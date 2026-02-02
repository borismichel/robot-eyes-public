/**
 * @file audio_player.h
 * @brief MP3 audio playback via ES8311 codec with full-duplex I2S
 *
 * This module provides MP3 file playback using the ESP8266Audio library.
 * Audio output uses a custom full-duplex I2S driver that allows simultaneous
 * microphone input for loud sound detection.
 *
 * ARCHITECTURE:
 * - MP3 decoding runs on a dedicated FreeRTOS task on Core 0
 * - Display/main loop runs on Core 1
 * - I2S bus is shared between TX (speaker) and RX (microphone)
 *
 * @author Robot Eyes Project
 * @date 2025
 */

#ifndef AUDIO_PLAYER_H
#define AUDIO_PLAYER_H

#include <Arduino.h>

// Forward declarations
class AudioGeneratorMP3;
class AudioFileSourceLittleFS;
class AudioOutput;

/**
 * @class AudioPlayer
 * @brief MP3 file player using full-duplex I2S for simultaneous playback/recording
 *
 * Usage:
 * @code
 *   AudioPlayer player;
 *   player.begin();
 *   player.play("/happy.mp3");
 *   // Microphone remains active during playback
 * @endcode
 */
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer();

    /**
     * @brief Initialize the audio subsystem
     *
     * Sets up LittleFS, ES8311 codec, full-duplex I2S, and the playback task.
     *
     * @return true if initialization successful
     */
    bool begin();

    /**
     * @brief Play an MP3 file from LittleFS
     *
     * Stops any current playback and starts the new file. Playback runs
     * asynchronously on the audio task.
     *
     * @param filename Path to MP3 file (e.g., "/happy.mp3")
     * @return true if playback started successfully
     */
    bool play(const char* filename);

    /**
     * @brief Stop current playback
     */
    void stop();

    /**
     * @brief Check if audio is currently playing
     * @return true if playback is active
     */
    bool isPlaying() const;

    /**
     * @brief Update function (no-op, audio runs on separate task)
     *
     * Provided for API compatibility. Actual playback is handled by
     * the background audio task.
     */
    void update();

    /**
     * @brief Internal task update - called from audio task
     *
     * Feeds the MP3 decoder with data. Called continuously by the
     * background audio task while playback is active.
     */
    void taskUpdate();

    /**
     * @brief Set playback volume
     * @param volume Volume level 0-100
     */
    void setVolume(int volume);

    /**
     * @brief Get current volume level
     * @return Volume 0-100
     */
    int getVolume() const { return volume; }

    /**
     * @brief Set microphone gain from slider value
     * @param sliderValue Slider value 0-100, where 50=0dB, 0=-24dB, 100=+42dB
     */
    void setMicGain(int sliderValue);

    /**
     * @brief Get current mic attenuation factor for software scaling
     * @return Attenuation multiplier (1.0 = no attenuation, 0.0625 = -24dB)
     */
    float getMicAttenuation() const { return micAttenuation; }

private:
    /**
     * @brief Initialize the ES8311 audio codec
     * @return true if successful
     */
    bool initCodec();

    bool initialized;       ///< Initialization state
    bool taskRunning;       ///< Audio task running state
    int volume;             ///< Current volume 0-100
    float micAttenuation;   ///< Mic software attenuation (1.0 = 0dB, <1.0 = negative gain)

    // ESP8266Audio components
    AudioGeneratorMP3* mp3;         ///< MP3 decoder
    AudioFileSourceLittleFS* file;  ///< Current audio file
    AudioOutput* out;               ///< Audio output (uses I2SDuplex)
};

#endif // AUDIO_PLAYER_H
