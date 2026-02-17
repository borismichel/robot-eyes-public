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

// Write buffer: 512 stereo pairs (2KB). Batching I2S writes reduces
// i2s_channel_write() calls from ~44K/sec to ~86/sec, eliminating
// choppy playback for longer audio (TTS responses).
#define AUDIO_OUT_BUF_FRAMES 512

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

    virtual bool begin() override;
    virtual bool ConsumeSample(int16_t sample[2]) override;
    virtual bool stop() override;
    virtual bool SetGain(float gain) override;
    virtual bool SetRate(int hz) override;

    /**
     * @brief Flush any buffered samples to I2S
     * Must be called after each generator->loop() to drain remaining samples.
     */
    void flush();

protected:
    I2SDuplex* i2s;
    float gain;
    bool started;
    uint32_t accumulator;  ///< Bresenham accumulator for rate conversion

    // Write buffer: accumulates samples before flushing to I2S in one write
    int16_t outBuf[AUDIO_OUT_BUF_FRAMES * 2];  // stereo pairs (L,R,L,R,...)
    size_t outBufPos;  // current position in outBuf (in int16_t units)

    void flushBuffer();
    inline void bufferSample(int16_t left, int16_t right) {
        outBuf[outBufPos++] = left;
        outBuf[outBufPos++] = right;
        if (outBufPos >= AUDIO_OUT_BUF_FRAMES * 2) {
            flushBuffer();
        }
    }
};

#endif // AUDIO_OUTPUT_DUPLEX_H
