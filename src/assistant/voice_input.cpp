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

    // Always monitor audio level
    I2SDuplex& i2s = I2SDuplex::getInstance();
    if (i2s.isInitialized() && i2s.isMicEnabled()) {
        currentLevel = i2s.getMicLevel();

        // Smooth the level for visualization
        const float smoothFactor = 0.3f;
        smoothedLevel = smoothedLevel * (1.0f - smoothFactor) + currentLevel * smoothFactor;
    }

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
    I2SDuplex& i2s = I2SDuplex::getInstance();
    if (!i2s.isInitialized()) return;

    // Read samples from I2S (44.1kHz stereo)
    size_t samplesRead = i2s.read(captureBuffer, VOICE_CAPTURE_SAMPLES);
    if (samplesRead == 0) return;

    // Downsample to 16kHz mono for speech recognition
    size_t downsampledCount;
    downsampleTo16kHz(captureBuffer, samplesRead, downsampleBuffer, &downsampledCount);

    // Write to ring buffer
    if (ringBuffer && downsampledCount > 0) {
        xSemaphoreTake(mutex, portMAX_DELAY);

        size_t bytesToWrite = downsampledCount * sizeof(int16_t);
        BaseType_t result = xRingbufferSend(ringBuffer, downsampleBuffer,
                                             bytesToWrite, pdMS_TO_TICKS(10));

        if (result != pdTRUE) {
            // Buffer full - drop oldest data
            // This happens if consumer is too slow
            // For now just log it
            static uint32_t lastOverflowLog = 0;
            if (millis() - lastOverflowLog > 1000) {
                Serial.println("[VoiceInput] WARNING: Ring buffer overflow");
                lastOverflowLog = millis();
            }
        }

        xSemaphoreGive(mutex);
    }
}

void VoiceInput::downsampleTo16kHz(const int16_t* src, size_t srcSamples,
                                    int16_t* dst, size_t* dstSamples) {
    // Source is 44.1kHz stereo, target is 16kHz mono
    // Ratio is 44100/16000 ≈ 2.756
    // We'll use a simple decimation with averaging

    // First, convert stereo to mono and then decimate
    // For 44.1kHz->16kHz, we take roughly 1 sample every 2.756 samples

    const float ratio = 44100.0f / 16000.0f;
    size_t outIdx = 0;
    float srcPos = 0.0f;

    while (srcPos < srcSamples && outIdx < (srcSamples / 3 + 1)) {
        size_t idx = (size_t)srcPos;

        // Stereo to mono: average left and right channels
        // I2S stereo is interleaved: L0,R0,L1,R1,...
        size_t stereoIdx = idx * 2;
        if (stereoIdx + 1 < srcSamples * 2) {
            int32_t left = src[stereoIdx];
            int32_t right = src[stereoIdx + 1];
            dst[outIdx] = (int16_t)((left + right) / 2);
        } else if (stereoIdx < srcSamples * 2) {
            dst[outIdx] = src[stereoIdx];
        }

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
