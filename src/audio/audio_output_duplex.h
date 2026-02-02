/**
 * @file audio_output_duplex.h
 * @brief Custom AudioOutput for ESP8266Audio using full-duplex I2S
 *
 * This class provides an AudioOutput implementation that uses our I2SDuplex
 * driver, allowing MP3 playback while the microphone remains active.
 *
 * @author Robot Eyes Project
 * @date 2025
 */

#ifndef AUDIO_OUTPUT_DUPLEX_H
#define AUDIO_OUTPUT_DUPLEX_H

#include <AudioOutput.h>
#include "i2s_duplex.h"

/**
 * @class AudioOutputDuplex
 * @brief AudioOutput implementation using full-duplex I2S
 *
 * Drop-in replacement for AudioOutputI2S that uses the I2SDuplex driver
 * for output while allowing simultaneous microphone input.
 */
class AudioOutputDuplex : public AudioOutput {
public:
    AudioOutputDuplex();
    virtual ~AudioOutputDuplex() override;

    /**
     * @brief Initialize the output
     * @return true if successful
     */
    virtual bool begin() override;

    /**
     * @brief Output a single sample
     * @param sample Pointer to sample data [left, right]
     * @return true if consumed
     */
    virtual bool ConsumeSample(int16_t sample[2]) override;

    /**
     * @brief Stop output
     * @return true
     */
    virtual bool stop() override;

    /**
     * @brief Set output gain
     * @param gain 0.0 to 4.0 (1.0 = unity)
     * @return true
     */
    virtual bool SetGain(float gain) override;

protected:
    I2SDuplex* i2s;
    float gain;
    bool started;
};

#endif // AUDIO_OUTPUT_DUPLEX_H
