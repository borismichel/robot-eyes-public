/**
 * @file i2s_duplex.h
 * @brief Full-duplex I2S driver for simultaneous playback and recording
 *
 * This driver manages the I2S peripheral in full-duplex mode, allowing
 * simultaneous audio output (MP3 playback) and input (microphone monitoring).
 *
 * Uses ESP-IDF I2S driver API for ESP32-S3.
 *
 * @author Robot Eyes Project
 * @date 2025
 */

#ifndef I2S_DUPLEX_H
#define I2S_DUPLEX_H

#include <Arduino.h>
#include <driver/i2s_std.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

//=============================================================================
// Configuration
//=============================================================================

/** Default sample rate for audio */
#define I2S_SAMPLE_RATE     44100

/** Bits per sample */
#define I2S_BITS_PER_SAMPLE 16

/** Number of DMA buffers */
#define I2S_DMA_BUF_COUNT   8

/** Samples per DMA buffer */
#define I2S_DMA_BUF_LEN     256

/** Microphone read buffer size (samples) */
#define MIC_BUFFER_SIZE     512

//=============================================================================
// I2SDuplex Class
//=============================================================================

/**
 * @class I2SDuplex
 * @brief Manages full-duplex I2S for simultaneous playback and recording
 *
 * This singleton class initializes the I2S peripheral with both TX (output)
 * and RX (input) channels enabled, allowing MP3 playback while monitoring
 * the microphone for loud sounds.
 */
class I2SDuplex {
public:
    /**
     * @brief Get singleton instance
     */
    static I2SDuplex& getInstance();

    /**
     * @brief Initialize I2S in full-duplex mode
     * @param sampleRate Sample rate in Hz (default 44100)
     * @return true if successful
     */
    bool begin(uint32_t sampleRate = I2S_SAMPLE_RATE);

    /**
     * @brief Shutdown I2S
     */
    void end();

    /**
     * @brief Check if initialized
     */
    bool isInitialized() const { return initialized; }

    //-------------------------------------------------------------------------
    // Output (Playback) Functions
    //-------------------------------------------------------------------------

    /**
     * @brief Write audio samples to I2S output
     * @param samples Pointer to sample buffer (interleaved stereo, 16-bit)
     * @param numSamples Number of samples to write
     * @return Number of samples actually written
     */
    size_t write(const int16_t* samples, size_t numSamples);

    /**
     * @brief Write a single stereo sample
     * @param left Left channel sample
     * @param right Right channel sample
     * @return true if written successfully
     */
    bool writeSample(int16_t left, int16_t right);

    //-------------------------------------------------------------------------
    // Input (Microphone) Functions
    //-------------------------------------------------------------------------

    /**
     * @brief Read audio samples from microphone
     * @param samples Buffer to store samples
     * @param numSamples Maximum samples to read
     * @return Number of samples actually read
     */
    size_t read(int16_t* samples, size_t numSamples);

    /**
     * @brief Get current microphone audio level (RMS)
     * @return Normalized level 0.0 to 1.0
     */
    float getMicLevel();

    /**
     * @brief Enable/disable microphone monitoring
     * @param enable true to enable
     */
    void setMicEnabled(bool enable);

    /**
     * @brief Check if microphone is enabled
     */
    bool isMicEnabled() const { return micEnabled; }

    /**
     * @brief Set software mic attenuation for negative gain
     * @param attenuation Multiplier (1.0 = 0dB, 0.0625 = -24dB)
     */
    void setMicAttenuation(float attenuation) { micAttenuation = attenuation; }

    //-------------------------------------------------------------------------
    // Configuration
    //-------------------------------------------------------------------------

    /**
     * @brief Set output volume
     * @param volume 0-100
     */
    void setVolume(int volume);

    /**
     * @brief Get current sample rate
     */
    uint32_t getSampleRate() const { return sampleRate; }

private:
    // Singleton
    I2SDuplex();
    ~I2SDuplex();
    I2SDuplex(const I2SDuplex&) = delete;
    I2SDuplex& operator=(const I2SDuplex&) = delete;

    bool initTxChannel();
    bool initRxChannel();

    bool initialized;
    bool micEnabled;
    uint32_t sampleRate;
    int volume;

    // I2S channel handles (ESP-IDF 5.x API)
    i2s_chan_handle_t txHandle;
    i2s_chan_handle_t rxHandle;

    // Thread safety
    SemaphoreHandle_t mutex;

    // Microphone level tracking
    float currentMicLevel;
    float micAttenuation;  // Software attenuation for negative gain (1.0 = 0dB)
    int16_t micBuffer[MIC_BUFFER_SIZE];
};

#endif // I2S_DUPLEX_H
