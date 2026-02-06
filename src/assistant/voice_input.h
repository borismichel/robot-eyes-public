/**
 * @file voice_input.h
 * @brief Voice input capture with ring buffer for streaming
 *
 * Captures audio from the microphone into a ring buffer for streaming
 * to speech-to-text services. Supports push-to-talk and voice activity
 * detection modes.
 */

#ifndef VOICE_INPUT_H
#define VOICE_INPUT_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/ringbuf.h>

//=============================================================================
// Configuration
//=============================================================================

/** Ring buffer size in bytes (holds ~2 seconds of 16kHz mono audio) */
#define VOICE_RING_BUFFER_SIZE (16000 * 2 * 2)

/** Audio sample rate for voice capture (Deepgram prefers 16kHz) */
#define VOICE_SAMPLE_RATE 16000

/** Samples to read per capture cycle */
#define VOICE_CAPTURE_SAMPLES 512

/** VAD silence threshold (normalized 0.0-1.0) */
#define VAD_SILENCE_THRESHOLD 0.02f

/** VAD silence duration to end utterance (ms) */
#define VAD_SILENCE_DURATION_MS 1500

/** VAD minimum speech duration to consider valid (ms) */
#define VAD_MIN_SPEECH_MS 200

//=============================================================================
// Voice Input State
//=============================================================================

/**
 * @enum VoiceInputState
 * @brief Current state of voice capture
 */
enum class VoiceInputState {
    Idle,           ///< Not capturing, waiting for activation
    Listening,      ///< Actively capturing audio
    Processing,     ///< Finished capturing, processing speech
    Speaking        ///< TTS is playing, ignore mic input
};

/**
 * @enum VoiceActivationMode
 * @brief How voice input is triggered
 */
enum class VoiceActivationMode {
    PushToTalk,     ///< Hold screen to speak
    WakeWord,       ///< "Hey Buddy" triggers listening
    Continuous      ///< Always listening with VAD
};

//=============================================================================
// VoiceInput Class
//=============================================================================

/**
 * @class VoiceInput
 * @brief Manages voice capture with ring buffer for streaming
 *
 * This class captures audio from the microphone and stores it in a ring
 * buffer. The audio can be read out for streaming to STT services.
 * Supports push-to-talk and VAD-based end-of-speech detection.
 */
class VoiceInput {
public:
    VoiceInput();
    ~VoiceInput();

    /**
     * @brief Initialize voice input
     * @return true if successful
     */
    bool begin();

    /**
     * @brief Cleanup and stop
     */
    void end();

    /**
     * @brief Update voice capture (call from main loop)
     * @param dt Delta time in seconds
     */
    void update(float dt);

    //-------------------------------------------------------------------------
    // Capture Control
    //-------------------------------------------------------------------------

    /**
     * @brief Start listening (for PTT or wake word trigger)
     */
    void startListening();

    /**
     * @brief Stop listening (for PTT release)
     */
    void stopListening();

    /**
     * @brief Notify that speech was detected (for wake word)
     */
    void onWakeWordDetected();

    /**
     * @brief Set speaking state (disables capture during TTS)
     */
    void setSpeaking(bool speaking);

    /**
     * @brief Clear the audio buffer
     */
    void clearBuffer();

    //-------------------------------------------------------------------------
    // Buffer Access
    //-------------------------------------------------------------------------

    /**
     * @brief Check if audio data is available
     * @return Number of bytes available in ring buffer
     */
    size_t available() const;

    /**
     * @brief Read audio data from buffer
     * @param buffer Destination buffer
     * @param maxBytes Maximum bytes to read
     * @return Bytes actually read
     */
    size_t read(uint8_t* buffer, size_t maxBytes);

    /**
     * @brief Peek at audio data without consuming
     * @param buffer Destination buffer
     * @param maxBytes Maximum bytes to peek
     * @return Bytes available
     */
    size_t peek(uint8_t* buffer, size_t maxBytes);

    //-------------------------------------------------------------------------
    // State Access
    //-------------------------------------------------------------------------

    /**
     * @brief Get current state
     */
    VoiceInputState getState() const { return state; }

    /**
     * @brief Check if actively capturing
     */
    bool isListening() const { return state == VoiceInputState::Listening; }

    /**
     * @brief Get current audio level (for visualization)
     */
    float getLevel() const { return currentLevel; }

    /**
     * @brief Check if speech is detected (VAD)
     */
    bool isSpeechDetected() const { return speechDetected; }

    /**
     * @brief Check if end of speech detected (silence after speech)
     */
    bool isEndOfSpeech() const { return endOfSpeechDetected; }

    /**
     * @brief Reset end of speech flag
     */
    void resetEndOfSpeech() { endOfSpeechDetected = false; }

    //-------------------------------------------------------------------------
    // Configuration
    //-------------------------------------------------------------------------

    /**
     * @brief Set activation mode
     */
    void setActivationMode(VoiceActivationMode mode) { activationMode = mode; }

    /**
     * @brief Get activation mode
     */
    VoiceActivationMode getActivationMode() const { return activationMode; }

    /**
     * @brief Set VAD sensitivity (0.0 = most sensitive, 1.0 = least)
     */
    void setVadSensitivity(float sensitivity);

    /**
     * @brief Enable/disable VAD
     */
    void setVadEnabled(bool enabled) { vadEnabled = enabled; }

private:
    /**
     * @brief Capture audio from I2S into ring buffer
     */
    void captureAudio();

    /**
     * @brief Downsample from 44.1kHz to 16kHz
     */
    void downsampleTo16kHz(const int16_t* src, size_t srcSamples,
                           int16_t* dst, size_t* dstSamples);

    /**
     * @brief Calculate RMS level of audio buffer
     */
    float calculateRMS(const int16_t* samples, size_t count);

    /**
     * @brief Update VAD state machine
     */
    void updateVAD(float level, float dt);

    bool initialized;
    VoiceInputState state;
    VoiceActivationMode activationMode;

    // Ring buffer for audio data
    RingbufHandle_t ringBuffer;
    SemaphoreHandle_t mutex;

    // Audio capture buffers
    int16_t captureBuffer[VOICE_CAPTURE_SAMPLES];
    int16_t downsampleBuffer[VOICE_CAPTURE_SAMPLES / 3 + 1];

    // Level tracking
    float currentLevel;
    float smoothedLevel;

    // Voice Activity Detection
    bool vadEnabled;
    bool speechDetected;
    bool endOfSpeechDetected;
    float vadThreshold;
    uint32_t speechStartTime;
    uint32_t silenceStartTime;
    uint32_t lastSpeechTime;
};

#endif // VOICE_INPUT_H
