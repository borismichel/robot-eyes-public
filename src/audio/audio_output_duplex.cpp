/**
 * @file audio_output_duplex.cpp
 * @brief Implementation of AudioOutputDuplex
 *
 * @author Robot Eyes Project
 * @date 2025
 */

#include "audio_output_duplex.h"

AudioOutputDuplex::AudioOutputDuplex()
    : i2s(nullptr)
    , gain(1.0f)
    , started(false)
    , accumulator(0)
    , outBufPos(0) {
}

AudioOutputDuplex::~AudioOutputDuplex() {
    stop();
}

bool AudioOutputDuplex::begin() {
    // Get the singleton I2S duplex instance
    i2s = &I2SDuplex::getInstance();

    // Initialize if not already done
    if (!i2s->isInitialized()) {
        if (!i2s->begin(44100)) {
            Serial.println("AudioOutputDuplex: Failed to initialize I2SDuplex");
            return false;
        }
    }

    outBufPos = 0;
    started = true;
    Serial.println("AudioOutputDuplex: Started");
    return true;
}

bool AudioOutputDuplex::ConsumeSample(int16_t sample[2]) {
    if (!started || !i2s) {
        return false;
    }

    // Apply gain and clamp
    int16_t left = (int16_t)constrain((int32_t)(sample[0] * gain), -32768, 32767);
    int16_t right = (int16_t)constrain((int32_t)(sample[1] * gain), -32768, 32767);

    // No rate conversion needed (or rate not set yet)
    if (hertz <= 0 || hertz == 44100) {
        bufferSample(left, right);
        return true;
    }

    // Rate conversion using Bresenham-like accumulator.
    // For each input sample (at hertz rate), output 44100/hertz samples.
    // e.g. 24kHz→44.1kHz: output ~1.84 samples per input sample.
    accumulator += 44100;
    while (accumulator >= (uint32_t)hertz) {
        accumulator -= hertz;
        bufferSample(left, right);
    }
    return true;
}

void AudioOutputDuplex::flushBuffer() {
    if (outBufPos > 0 && i2s) {
        i2s->write(outBuf, outBufPos);
        outBufPos = 0;
    }
}

void AudioOutputDuplex::flush() {
    flushBuffer();
}

bool AudioOutputDuplex::SetRate(int hz) {
    Serial.printf("AudioOutputDuplex: SetRate(%d) -> I2S at 44100\n", hz);
    AudioOutput::SetRate(hz);
    accumulator = 0;
    return true;
}

bool AudioOutputDuplex::stop() {
    flushBuffer();
    started = false;
    // Don't shut down I2S - leave it running for microphone
    return true;
}

bool AudioOutputDuplex::SetGain(float g) {
    gain = g;
    return true;
}
