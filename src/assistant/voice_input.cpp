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
    static bool dumpedRaw = false;  // One-shot raw sample dump

    I2SDuplex& i2s = I2SDuplex::getInstance();
    if (!i2s.isInitialized()) return;

    // Read up to 8 batches (matching DMA buffer count) per frame.
    // Non-blocking ring buffer send — break if full (consumer needs to catch up).
    for (int reads = 0; reads < 8; reads++) {
        size_t samplesRead = i2s.read(captureBuffer, VOICE_CAPTURE_SAMPLES);
        if (samplesRead == 0) break;

        // I2S RX returns stereo interleaved data (L,R,L,R,...) because TX and
        // RX share the same I2S bus configured for stereo.  Extract left channel.
        size_t frames = samplesRead / 2;  // stereo frames
        for (size_t i = 0; i < frames; i++) {
            captureBuffer[i] = captureBuffer[i * 2];  // left channel only
        }

        // One-shot: dump raw samples for diagnosis
        if (!dumpedRaw && frames >= 10) {
            dumpedRaw = true;
            Serial.printf("[VoiceInput] RAW left-channel samples (%d frames from %d i16s), first 10:\n", frames, samplesRead);
            for (int i = 0; i < 10 && i < (int)frames; i++) {
                Serial.printf("  [%d] = %6d\n", i, captureBuffer[i]);
            }
            int16_t minV = 32767, maxV = -32768;
            int64_t sumAbs = 0;
            for (size_t i = 0; i < frames; i++) {
                if (captureBuffer[i] < minV) minV = captureBuffer[i];
                if (captureBuffer[i] > maxV) maxV = captureBuffer[i];
                sumAbs += abs(captureBuffer[i]);
            }
            Serial.printf("[VoiceInput] Stats: min=%d max=%d avg=%.1f monoSamples=%d\n",
                          minV, maxV, (float)sumAbs / frames, frames);
        }

        // Update mic level from captured data (avoids getMicLevel() which
        // would consume I2S data separately, stealing audio from capture)
        currentLevel = calculateRMS(captureBuffer, frames);

        // Downsample to 16kHz mono for speech recognition
        size_t downsampledCount;
        downsampleTo16kHz(captureBuffer, frames, downsampleBuffer, &downsampledCount);

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
