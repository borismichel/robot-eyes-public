/**
 * @file audio_handler.h
 * @brief Microphone input handler for loud sound detection
 *
 * Monitors microphone input via the full-duplex I2S driver to detect loud
 * sounds that trigger irritated reactions. Works simultaneously with MP3
 * playback since both share the I2S bus in full-duplex mode.
 *
 * @author Robot Eyes Project
 * @date 2025
 */

#ifndef AUDIO_HANDLER_H
#define AUDIO_HANDLER_H

#include <Arduino.h>

//=============================================================================
// Constants
//=============================================================================

/** ES8311 codec I2C address */
#define ES8311_ADDR 0x18

//=============================================================================
// Audio Events
//=============================================================================

/**
 * @enum AudioEvent
 * @brief Events detected by the audio handler
 */
enum class AudioEvent {
    None,        ///< No event
    TooLoud      ///< Environment too loud - trigger irritated reaction
};

//=============================================================================
// AudioHandler Class
//=============================================================================

/**
 * @class AudioHandler
 * @brief Monitors microphone for loud sounds to trigger reactions
 *
 * This handler uses the full-duplex I2S driver's RX channel to read
 * microphone input. It calculates RMS levels and triggers events when
 * the audio level exceeds a configurable threshold.
 *
 * The handler works simultaneously with MP3 playback since the I2S bus
 * operates in full-duplex mode (TX for speaker, RX for microphone).
 */
class AudioHandler {
public:
    AudioHandler();

    /**
     * @brief Initialize the audio handler
     *
     * Sets up microphone monitoring via the full-duplex I2S driver.
     * Should be called after AudioPlayer::begin() which initializes the I2S bus.
     *
     * @return true if successful
     */
    bool begin();

    /**
     * @brief Update audio monitoring
     *
     * Reads microphone level and checks for threshold crossing.
     * Should be called every frame.
     *
     * @param dt Delta time in seconds
     * @return AudioEvent::TooLoud if threshold exceeded, else AudioEvent::None
     */
    AudioEvent update(float dt);

    //-------------------------------------------------------------------------
    // Level Accessors
    //-------------------------------------------------------------------------

    /**
     * @brief Get instantaneous audio level
     * @return Normalized level 0.0 to 1.0
     */
    float getLevel() const { return currentLevel; }

    /**
     * @brief Get smoothed audio level (for visualization)
     * @return Smoothed normalized level 0.0 to 1.0
     */
    float getSmoothedLevel() const { return smoothedLevel; }

    /**
     * @brief Get peak audio level (decays over time)
     * @return Peak normalized level 0.0 to 1.0
     */
    float getPeakLevel() const { return peakLevel; }

    //-------------------------------------------------------------------------
    // Configuration
    //-------------------------------------------------------------------------

    /**
     * @brief Set detection threshold
     * @param threshold Value 0.0 to 1.0 (lower = more sensitive)
     */
    void setThreshold(float threshold) { this->threshold = threshold; }

    /**
     * @brief Get current detection threshold
     */
    float getThreshold() const { return threshold; }

    /**
     * @brief Enable or disable audio monitoring
     * @param enabled true to enable
     */
    void setEnabled(bool enabled) { this->enabled = enabled; }

    /**
     * @brief Check if monitoring is enabled
     */
    bool isEnabled() const { return enabled; }

private:
    /**
     * @brief Read current audio level from microphone
     * @return Normalized level 0.0 to 1.0
     */
    float readAudioLevel();

    bool initialized;       ///< Initialization state
    bool enabled;           ///< Monitoring enabled state

    // Audio levels
    float currentLevel;     ///< Instantaneous level
    float smoothedLevel;    ///< Smoothed level (for display)
    float peakLevel;        ///< Peak level with decay
    float threshold;        ///< Detection threshold

    // Debounce timing
    uint32_t lastTriggerTime;
    static constexpr uint32_t DEBOUNCE_MS = 2000;  ///< Min time between triggers (2 seconds)
};

#endif // AUDIO_HANDLER_H
