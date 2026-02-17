/**
 * @file voice_input.cpp
 * @brief Voice input capture implementation
 */

#include "voice_input.h"
#include "../audio/i2s_duplex.h"

//=============================================================================
// Constructor / Destructor
//=============================================================================

VoiceInput::VoiceInput()
    : initialized(false)
    , state(VoiceInputState::Idle)
    , activationMode(VoiceActivationMode::PushToTalk)
    , ringBuffer(nullptr)
    , mutex(nullptr)
    , currentLevel(0.0f)
    , smoothedLevel(0.0f)
    , vadEnabled(true)
    , speechDetected(false)
    , endOfSpeechDetected(false)
    , vadThreshold(VAD_SILENCE_THRESHOLD)
    , speechStartTime(0)
    , silenceStartTime(0)
    , lastSpeechTime(0)
    , hpfPrevInput(0.0f)
    , hpfPrevOutput(0.0f)
    , audioDataCallback(nullptr)
{
    memset(captureBuffer, 0, sizeof(captureBuffer));
    memset(downsampleBuffer, 0, sizeof(downsampleBuffer));
}

VoiceInput::~VoiceInput() {
    end();
}

//=============================================================================
// Initialization
//=============================================================================

bool VoiceInput::begin() {
    if (initialized) return true;

    Serial.println("[VoiceInput] Initializing...");

    // Create ring buffer
    ringBuffer = xRingbufferCreate(VOICE_RING_BUFFER_SIZE, RINGBUF_TYPE_BYTEBUF);
    if (!ringBuffer) {
        Serial.println("[VoiceInput] ERROR: Failed to create ring buffer");
        return false;
    }

    // Create mutex
    mutex = xSemaphoreCreateMutex();
    if (!mutex) {
        Serial.println("[VoiceInput] ERROR: Failed to create mutex");
        vRingbufferDelete(ringBuffer);
        ringBuffer = nullptr;
        return false;
    }

    // Verify I2S is initialized
    I2SDuplex& i2s = I2SDuplex::getInstance();
    if (!i2s.isInitialized()) {
        Serial.println("[VoiceInput] WARNING: I2S not initialized, mic may not work");
    }

    initialized = true;
    Serial.println("[VoiceInput] Initialized successfully");
    return true;
}

void VoiceInput::end() {
    if (!initialized) return;

    state = VoiceInputState::Idle;

    if (mutex) {
        vSemaphoreDelete(mutex);
        mutex = nullptr;
    }

    if (ringBuffer) {
        vRingbufferDelete(ringBuffer);
        ringBuffer = nullptr;
    }

    initialized = false;
    Serial.println("[VoiceInput] Shutdown");
}

//=============================================================================
// Main Update Loop
//=============================================================================

void VoiceInput::update(float dt) {
    if (!initialized) return;

    // Don't capture during TTS playback
    if (state == VoiceInputState::Speaking) {
        return;
    }

    // Monitor audio level — but NOT during Listening, because getMicLevel()
    // reads from I2S (consuming DMA data) which would steal audio from captureAudio().
    // During Listening, captureAudio() updates currentLevel instead.
    if (state != VoiceInputState::Listening) {
        I2SDuplex& i2s = I2SDuplex::getInstance();
        if (i2s.isInitialized() && i2s.isMicEnabled()) {
            currentLevel = i2s.getMicLevel();
        }
    }

    // Smooth the level for visualization
    const float smoothFactor = 0.3f;
    smoothedLevel = smoothedLevel * (1.0f - smoothFactor) + currentLevel * smoothFactor;

    // Capture audio when listening
    if (state == VoiceInputState::Listening) {
        captureAudio();

        // Update VAD
        if (vadEnabled) {
            updateVAD(currentLevel, dt);

            // Check for end of speech in PTT mode still uses VAD for better UX
            if (activationMode == VoiceActivationMode::PushToTalk) {
                // In PTT mode, VAD is informational but doesn't stop capture
            } else {
                // In other modes, end of speech triggers processing
                if (endOfSpeechDetected) {
                    Serial.println("[VoiceInput] End of speech detected");
                    state = VoiceInputState::Processing;
                }
            }
        }
    }
}

//=============================================================================
// Capture Control
//=============================================================================

void VoiceInput::startListening() {
    if (!initialized) return;

    Serial.println("[VoiceInput] Start listening");
    clearBuffer();

    state = VoiceInputState::Listening;
    speechDetected = false;
    endOfSpeechDetected = false;
    speechStartTime = 0;
    silenceStartTime = 0;
    hpfPrevInput = 0.0f;
    hpfPrevOutput = 0.0f;

    // Enable mic if not already
    I2SDuplex& i2s = I2SDuplex::getInstance();
    i2s.setMicEnabled(true);
}

void VoiceInput::stopListening() {
    if (!initialized) return;

    if (state == VoiceInputState::Listening) {
        Serial.println("[VoiceInput] Stop listening");
        state = VoiceInputState::Processing;
    }
}

void VoiceInput::onWakeWordDetected() {
    if (!initialized) return;

    Serial.println("[VoiceInput] Wake word detected!");
    startListening();
}

void VoiceInput::setSpeaking(bool speaking) {
    if (speaking) {
        state = VoiceInputState::Speaking;
    } else {
        state = VoiceInputState::Idle;
    }
}

void VoiceInput::clearBuffer() {
    if (!ringBuffer) return;

    xSemaphoreTake(mutex, portMAX_DELAY);

    // Read and discard all data
    size_t itemSize;
    void* item;
    while ((item = xRingbufferReceive(ringBuffer, &itemSize, 0)) != nullptr) {
        vRingbufferReturnItem(ringBuffer, item);
    }

    xSemaphoreGive(mutex);
}

//=============================================================================
// Buffer Access
//=============================================================================

size_t VoiceInput::available() const {
    if (!ringBuffer) return 0;

    // Get bytes waiting in ring buffer
    size_t freeSpace = xRingbufferGetCurFreeSize(ringBuffer);
    return VOICE_RING_BUFFER_SIZE - freeSpace;
}

size_t VoiceInput::read(uint8_t* buffer, size_t maxBytes) {
    if (!ringBuffer || !buffer || maxBytes == 0) return 0;

    xSemaphoreTake(mutex, portMAX_DELAY);

    size_t totalRead = 0;
    size_t itemSize;

    // Read chunks from ring buffer
    while (totalRead < maxBytes) {
        size_t toRead = maxBytes - totalRead;
        void* item = xRingbufferReceiveUpTo(ringBuffer, &itemSize, 0, toRead);

        if (!item) break;

        memcpy(buffer + totalRead, item, itemSize);
        totalRead += itemSize;
        vRingbufferReturnItem(ringBuffer, item);
    }

    xSemaphoreGive(mutex);
    return totalRead;
}

size_t VoiceInput::peek(uint8_t* buffer, size_t maxBytes) {
    // Note: FreeRTOS ring buffer doesn't support true peek
    // This is a limitation - would need custom implementation
    return 0;
}

//=============================================================================
// Audio Capture
//=============================================================================

void VoiceInput::captureAudio() {
    I2SDuplex& i2s = I2SDuplex::getInstance();
    if (!i2s.isInitialized()) return;

    // Read up to 24 batches per frame to drain the RX DMA buffer (12×1024 frames).
    for (int reads = 0; reads < 24; reads++) {
        size_t samplesRead = i2s.read(captureBuffer, VOICE_CAPTURE_SAMPLES);
        if (samplesRead == 0) break;

        // I2S RX is stereo (shared bus with stereo TX). ES8311 ADC is mono,
        // outputting on left channel only. Extract left from interleaved L,R,L,R.
        size_t monoSamples = samplesRead / 2;
        for (size_t i = 0; i < monoSamples; i++) {
            captureBuffer[i] = captureBuffer[i * 2];
        }

        // DC offset removal: single-pole high-pass filter
        // y[n] = alpha * (y[n-1] + x[n] - x[n-1])
        // alpha = 0.995 gives ~35Hz cutoff at 44.1kHz, removing DC bias from the ADC
        const float alpha = 0.995f;
        float atten = i2s.getMicAttenuation();
        int32_t peak = 0;
        for (size_t i = 0; i < monoSamples; i++) {
            float input = (float)captureBuffer[i];
            hpfPrevOutput = alpha * (hpfPrevOutput + input - hpfPrevInput);
            hpfPrevInput = input;
            // Apply mic attenuation (from gain slider)
            float out = hpfPrevOutput * atten;
            int32_t sample = constrain((int32_t)out, -32767, 32767);
            captureBuffer[i] = (int16_t)sample;
            int32_t absSample = sample < 0 ? -sample : sample;
            if (absSample > peak) peak = absSample;
        }

        // Peak normalization: if signal is clipping (peak > 28000 ≈ 85% full scale),
        // scale all samples down to target peak of 24000 (~73% full scale).
        // This prevents Whisper hallucinations from flat-topped clipped waveforms.
        if (peak > 28000) {
            float normGain = 24000.0f / (float)peak;
            for (size_t i = 0; i < monoSamples; i++) {
                captureBuffer[i] = (int16_t)((float)captureBuffer[i] * normGain);
            }
        }

        // Update mic level from captured data
        currentLevel = calculateRMS(captureBuffer, monoSamples);

        // Downsample to 16kHz mono for speech recognition
        size_t downsampledCount;
        downsampleTo16kHz(captureBuffer, monoSamples, downsampleBuffer, &downsampledCount);

        if (downsampledCount > 0) {
            size_t bytesToWrite = downsampledCount * sizeof(int16_t);

            // Direct callback path: send audio straight to STT (no ring buffer)
            if (audioDataCallback) {
                audioDataCallback((const uint8_t*)downsampleBuffer, bytesToWrite);
            } else if (ringBuffer) {
                // Fallback: ring buffer path
                xSemaphoreTake(mutex, portMAX_DELAY);
                BaseType_t result = xRingbufferSend(ringBuffer, downsampleBuffer,
                                                     bytesToWrite, 0);
                xSemaphoreGive(mutex);

                if (result != pdTRUE) {
                    Serial.println("[VoiceInput] Ring buffer full, dropping audio");
                    break;
                }
            }
        }
    }
}

void VoiceInput::downsampleTo16kHz(const int16_t* src, size_t srcSamples,
                                    int16_t* dst, size_t* dstSamples) {
    // Source is 44.1kHz MONO from I2S RX (configured as mono left-channel only).
    // srcSamples = number of mono samples at 44.1kHz.
    // Downsample ratio: 44100/16000 ≈ 2.756 samples per output sample.
    //
    // Use averaging (box filter) to prevent aliasing.

    const float ratio = 44100.0f / 16000.0f;
    size_t outIdx = 0;
    float srcPos = 0.0f;

    while (srcPos < (float)srcSamples && outIdx < (VOICE_CAPTURE_SAMPLES / 2)) {
        size_t startSample = (size_t)srcPos;
        size_t endSample = (size_t)(srcPos + ratio);
        if (endSample > srcSamples) endSample = srcSamples;
        if (endSample <= startSample) endSample = startSample + 1;

        // Average samples in this window
        int32_t sum = 0;
        for (size_t s = startSample; s < endSample; s++) {
            sum += src[s];
        }
        dst[outIdx] = (int16_t)(sum / (int32_t)(endSample - startSample));

        outIdx++;
        srcPos += ratio;
    }

    *dstSamples = outIdx;
}

float VoiceInput::calculateRMS(const int16_t* samples, size_t count) {
    if (count == 0) return 0.0f;

    int64_t sum = 0;
    for (size_t i = 0; i < count; i++) {
        int32_t sample = samples[i];
        sum += sample * sample;
    }

    float rms = sqrtf((float)sum / count);
    return rms / 32768.0f;  // Normalize to 0.0-1.0
}

//=============================================================================
// Voice Activity Detection
//=============================================================================

void VoiceInput::updateVAD(float level, float dt) {
    uint32_t now = millis();

    if (level > vadThreshold) {
        // Speech detected
        if (!speechDetected) {
            speechStartTime = now;
        }
        speechDetected = true;
        lastSpeechTime = now;
        silenceStartTime = 0;
    } else {
        // Silence
        if (speechDetected && silenceStartTime == 0) {
            silenceStartTime = now;
        }

        // Check if silence duration indicates end of speech
        if (speechDetected && silenceStartTime > 0) {
            uint32_t silenceDuration = now - silenceStartTime;
            uint32_t speechDuration = lastSpeechTime - speechStartTime;

            // Only consider end of speech if we had meaningful speech
            if (silenceDuration >= VAD_SILENCE_DURATION_MS &&
                speechDuration >= VAD_MIN_SPEECH_MS) {
                endOfSpeechDetected = true;
            }
        }
    }
}

void VoiceInput::setVadSensitivity(float sensitivity) {
    // Map sensitivity 0.0-1.0 to threshold
    // Lower sensitivity = lower threshold = more sensitive
    vadThreshold = VAD_SILENCE_THRESHOLD * (0.5f + sensitivity * 1.5f);
}
