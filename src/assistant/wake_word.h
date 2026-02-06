/**
 * @file wake_word.h
 * @brief Wake word detection using ESP-SR (Espressif Speech Recognition)
 *
 * Detects "Hey Buddy" wake word to activate the voice assistant.
 * Uses ESP-SR which runs locally on ESP32-S3 without external services.
 *
 * ESP-SR Features:
 * - Runs entirely on-device (no cloud required)
 * - Optimized for ESP32-S3 vector instructions
 * - Built-in wake words: "Hi ESP", "Alexa", etc.
 * - Custom wake words via offline training (used for "Hey Buddy")
 *
 * NOTE: Full ESP-SR requires adding esp-sr component to platformio.ini:
 *   lib_deps =
 *     https://github.com/espressif/esp-sr.git
 *
 * Until configured, this provides a stub that can be triggered manually.
 */

#ifndef WAKE_WORD_H
#define WAKE_WORD_H

#include <Arduino.h>
#include <functional>

//=============================================================================
// Configuration
//=============================================================================

/** Audio frame size for wake word processing (samples) */
#define WAKE_WORD_FRAME_SIZE 512

/** Sample rate for wake word detection */
#define WAKE_WORD_SAMPLE_RATE 16000

/** Default sensitivity (0.0-1.0) */
#define WAKE_WORD_DEFAULT_SENSITIVITY 0.5f

/** Built-in wake word options */
#define WAKE_WORD_HI_ESP      0
#define WAKE_WORD_ALEXA       1
#define WAKE_WORD_CUSTOM      2

//=============================================================================
// Callbacks
//=============================================================================

/**
 * @brief Callback when wake word is detected
 */
using WakeWordCallback = std::function<void()>;

//=============================================================================
// WakeWordDetector Class
//=============================================================================

/**
 * @class WakeWordDetector
 * @brief Detects wake word in audio stream using ESP-SR
 *
 * Process audio frames through this class to detect the wake word.
 * When detected, the callback is invoked.
 */
class WakeWordDetector {
public:
    WakeWordDetector();
    ~WakeWordDetector();

    /**
     * @brief Initialize wake word detection
     * @param wakeWordId Which wake word to use (WAKE_WORD_HI_ESP, etc.)
     * @return true if initialization successful
     */
    bool begin(int wakeWordId = WAKE_WORD_CUSTOM);

    /**
     * @brief Cleanup
     */
    void end();

    /**
     * @brief Process audio frame for wake word detection
     * @param samples Audio samples (16-bit mono, 16kHz)
     * @param count Number of samples
     * @return true if wake word was detected in this frame
     */
    bool process(const int16_t* samples, size_t count);

    /**
     * @brief Manually trigger wake word (for testing/buttons)
     */
    void trigger();

    //-------------------------------------------------------------------------
    // Configuration
    //-------------------------------------------------------------------------

    /**
     * @brief Set detection sensitivity
     * @param sensitivity 0.0 (least sensitive) to 1.0 (most sensitive)
     */
    void setSensitivity(float sensitivity);

    /**
     * @brief Get current sensitivity
     */
    float getSensitivity() const { return sensitivity; }

    /**
     * @brief Enable/disable detection
     */
    void setEnabled(bool enabled) { this->enabled = enabled; }

    /**
     * @brief Check if detection is enabled
     */
    bool isEnabled() const { return enabled; }

    /**
     * @brief Check if ESP-SR is available
     */
    bool isESPSRAvailable() const { return espSrAvailable; }

    /**
     * @brief Get wake word name
     */
    const char* getWakeWordName() const;

    //-------------------------------------------------------------------------
    // Callback
    //-------------------------------------------------------------------------

    /**
     * @brief Set wake word detected callback
     */
    void onWakeWord(WakeWordCallback callback) { wakeWordCallback = callback; }

private:
    bool initialized;
    bool enabled;
    bool espSrAvailable;
    float sensitivity;
    int wakeWordId;

    // ESP-SR handle (void* to avoid including headers when not available)
    void* srHandle;

    // Audio buffer for frame accumulation
    int16_t frameBuffer[WAKE_WORD_FRAME_SIZE];
    size_t frameIndex;

    // Callback
    WakeWordCallback wakeWordCallback;

    /**
     * @brief Process a complete frame
     */
    bool processFrame();
};

#endif // WAKE_WORD_H
