/**
 * @file audio_handler.cpp
 * @brief Microphone input handler for loud sound detection
 *
 * Monitors the microphone via the full-duplex I2S driver to detect loud sounds
 * that can trigger irritated reactions. Works simultaneously with MP3 playback.
 *
 * @author Robot Eyes Project
 * @date 2025
 */

#include "audio_handler.h"
#include "../audio/i2s_duplex.h"
#include <cmath>

//=============================================================================
// Constructor
//=============================================================================

AudioHandler::AudioHandler()
    : initialized(false)
    , enabled(true)
    , currentLevel(0.0f)
    , smoothedLevel(0.0f)
    , peakLevel(0.0f)
    , threshold(0.95f)  // Default threshold - very high, only triggers on loud sounds
    , lastTriggerTime(0) {
}

//=============================================================================
// Initialization
//=============================================================================

/**
 * @brief Initialize the audio handler
 *
 * Uses the full-duplex I2S driver for microphone input. The I2S bus is
 * shared with audio playback, allowing simultaneous MP3 output and
 * microphone monitoring.
 *
 * @return true if successful
 */
bool AudioHandler::begin() {
    // Get reference to the full-duplex I2S driver
    I2SDuplex& i2s = I2SDuplex::getInstance();

    // Check if I2S is already initialized (by AudioPlayer)
    if (!i2s.isInitialized()) {
        Serial.println("AudioHandler: I2S not yet initialized, waiting for AudioPlayer");
        // I2S will be initialized by AudioPlayer::begin()
        // We just verify we can get the singleton
    }

    // Enable microphone input
    i2s.setMicEnabled(true);

    initialized = true;
    Serial.println("AudioHandler: Initialized (simple amplitude detection)");
    return true;
}

//=============================================================================
// Audio Level Reading
//=============================================================================

/**
 * @brief Read current microphone audio level
 *
 * Gets the RMS level from the I2S microphone input. This works
 * simultaneously with MP3 playback.
 *
 * @return Normalized audio level 0.0 to 1.0
 */
float AudioHandler::readAudioLevel() {
    if (!initialized || !enabled) {
        return 0.0f;
    }

    // Get microphone level from the full-duplex I2S driver
    I2SDuplex& i2s = I2SDuplex::getInstance();

    if (!i2s.isInitialized() || !i2s.isMicEnabled()) {
        return 0.0f;
    }

    return i2s.getMicLevel();
}

//=============================================================================
// Update Loop
//=============================================================================

/**
 * @brief Update audio monitoring and check for loud sounds
 *
 * Should be called each frame. Updates the smoothed audio level and
 * triggers TooLoud event when threshold is exceeded.
 *
 * @param dt Delta time in seconds
 * @return AudioEvent::TooLoud if threshold exceeded, else AudioEvent::None
 */
AudioEvent AudioHandler::update(float dt) {
    if (!initialized || !enabled) {
        currentLevel = 0.0f;
        smoothedLevel = 0.0f;
        return AudioEvent::None;
    }

    // Read current audio level from microphone
    currentLevel = readAudioLevel();

    // Smooth the level for display (fast attack, slow decay)
    if (currentLevel > smoothedLevel) {
        smoothedLevel = smoothedLevel + (currentLevel - smoothedLevel) * 0.5f;
    } else {
        smoothedLevel = smoothedLevel + (currentLevel - smoothedLevel) * 0.1f;
    }

    // Track peak with decay
    if (currentLevel > peakLevel) {
        peakLevel = currentLevel;
    } else {
        peakLevel *= 0.95f;
    }

    // Check for loud sound (above threshold and not in debounce period)
    uint32_t now = millis();
    if (currentLevel > threshold && (now - lastTriggerTime > DEBOUNCE_MS)) {
        lastTriggerTime = now;
        Serial.printf("Too loud! Level: %.2f (threshold: %.2f)\n", currentLevel, threshold);
        return AudioEvent::TooLoud;
    }

    return AudioEvent::None;
}
