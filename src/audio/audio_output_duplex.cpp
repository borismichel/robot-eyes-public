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
    , started(false) {
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

    started = true;
    Serial.println("AudioOutputDuplex: Started");
    return true;
}

bool AudioOutputDuplex::ConsumeSample(int16_t sample[2]) {
    if (!started || !i2s) {
        return false;
    }

    // Apply gain
    int32_t left = (int32_t)(sample[0] * gain);
    int32_t right = (int32_t)(sample[1] * gain);

    // Clamp to 16-bit range
    left = constrain(left, -32768, 32767);
    right = constrain(right, -32768, 32767);

    // Write to I2S
    return i2s->writeSample((int16_t)left, (int16_t)right);
}

bool AudioOutputDuplex::stop() {
    started = false;
    // Don't shut down I2S - leave it running for microphone
    return true;
}

bool AudioOutputDuplex::SetGain(float g) {
    gain = g;
    return true;
}
