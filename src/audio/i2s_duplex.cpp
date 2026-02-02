/**
 * @file i2s_duplex.cpp
 * @brief Full-duplex I2S driver implementation
 *
 * Implements simultaneous I2S TX (playback) and RX (microphone) using
 * the ESP-IDF I2S standard mode driver.
 *
 * @author Robot Eyes Project
 * @date 2025
 */

#include "i2s_duplex.h"
#include "pin_config.h"
#include <cmath>

//=============================================================================
// Singleton Instance
//=============================================================================

I2SDuplex& I2SDuplex::getInstance() {
    static I2SDuplex instance;
    return instance;
}

//=============================================================================
// Constructor / Destructor
//=============================================================================

I2SDuplex::I2SDuplex()
    : initialized(false)
    , micEnabled(true)
    , sampleRate(I2S_SAMPLE_RATE)
    , volume(80)
    , txHandle(nullptr)
    , rxHandle(nullptr)
    , mutex(nullptr)
    , currentMicLevel(0.0f)
    , micAttenuation(1.0f) {  // No attenuation by default (0dB)
    memset(micBuffer, 0, sizeof(micBuffer));
}

I2SDuplex::~I2SDuplex() {
    end();
}

//=============================================================================
// Initialization
//=============================================================================

bool I2SDuplex::begin(uint32_t rate) {
    if (initialized) {
        return true;
    }

    sampleRate = rate;

    // Create mutex for thread safety
    mutex = xSemaphoreCreateMutex();
    if (!mutex) {
        Serial.println("I2SDuplex: Failed to create mutex");
        return false;
    }

    // Initialize TX channel (output/playback)
    if (!initTxChannel()) {
        Serial.println("I2SDuplex: Failed to init TX channel");
        vSemaphoreDelete(mutex);
        mutex = nullptr;
        return false;
    }

    // Initialize RX channel (input/microphone)
    if (!initRxChannel()) {
        Serial.println("I2SDuplex: Failed to init RX channel");
        i2s_del_channel(txHandle);
        txHandle = nullptr;
        vSemaphoreDelete(mutex);
        mutex = nullptr;
        return false;
    }

    // Enable both channels
    esp_err_t err = i2s_channel_enable(txHandle);
    if (err != ESP_OK) {
        Serial.printf("I2SDuplex: Failed to enable TX channel: %d\n", err);
        end();
        return false;
    }

    err = i2s_channel_enable(rxHandle);
    if (err != ESP_OK) {
        Serial.printf("I2SDuplex: Failed to enable RX channel: %d\n", err);
        end();
        return false;
    }

    initialized = true;
    Serial.printf("I2SDuplex: Initialized at %d Hz (full-duplex)\n", sampleRate);
    return true;
}

bool I2SDuplex::initTxChannel() {
    // Channel configuration
    i2s_chan_config_t chanCfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = I2S_DMA_BUF_COUNT,
        .dma_frame_num = I2S_DMA_BUF_LEN,
        .auto_clear = true,  // Auto clear DMA buffer on underflow
    };

    // Create TX channel
    esp_err_t err = i2s_new_channel(&chanCfg, &txHandle, nullptr);
    if (err != ESP_OK) {
        Serial.printf("I2SDuplex: i2s_new_channel TX failed: %d\n", err);
        return false;
    }

    // Standard mode configuration for TX
    i2s_std_config_t stdCfg = {
        .clk_cfg = {
            .sample_rate_hz = sampleRate,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = (gpio_num_t)I2S_MCK_IO,
            .bclk = (gpio_num_t)I2S_BCK_IO,
            .ws = (gpio_num_t)I2S_WS_IO,
            .dout = (gpio_num_t)I2S_DO_IO,
            .din = GPIO_NUM_NC,  // TX only, no input
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(txHandle, &stdCfg);
    if (err != ESP_OK) {
        Serial.printf("I2SDuplex: i2s_channel_init_std_mode TX failed: %d\n", err);
        i2s_del_channel(txHandle);
        txHandle = nullptr;
        return false;
    }

    Serial.println("I2SDuplex: TX channel initialized");
    return true;
}

bool I2SDuplex::initRxChannel() {
    // Channel configuration for RX
    i2s_chan_config_t chanCfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = I2S_DMA_BUF_COUNT,
        .dma_frame_num = I2S_DMA_BUF_LEN,
        .auto_clear = false,
    };

    // Create RX channel (uses same I2S peripheral as TX for full-duplex)
    esp_err_t err = i2s_new_channel(&chanCfg, nullptr, &rxHandle);
    if (err != ESP_OK) {
        Serial.printf("I2SDuplex: i2s_new_channel RX failed: %d\n", err);
        return false;
    }

    // Standard mode configuration for RX
    i2s_std_config_t stdCfg = {
        .clk_cfg = {
            .sample_rate_hz = sampleRate,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false,
        },
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,  // MCLK shared, don't reconfigure
            .bclk = GPIO_NUM_NC,  // BCLK shared
            .ws = GPIO_NUM_NC,    // WS shared
            .dout = GPIO_NUM_NC,  // RX only, no output
            .din = (gpio_num_t)I2S_DI_IO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(rxHandle, &stdCfg);
    if (err != ESP_OK) {
        Serial.printf("I2SDuplex: i2s_channel_init_std_mode RX failed: %d\n", err);
        i2s_del_channel(rxHandle);
        rxHandle = nullptr;
        return false;
    }

    Serial.println("I2SDuplex: RX channel initialized");
    return true;
}

void I2SDuplex::end() {
    if (txHandle) {
        i2s_channel_disable(txHandle);
        i2s_del_channel(txHandle);
        txHandle = nullptr;
    }

    if (rxHandle) {
        i2s_channel_disable(rxHandle);
        i2s_del_channel(rxHandle);
        rxHandle = nullptr;
    }

    if (mutex) {
        vSemaphoreDelete(mutex);
        mutex = nullptr;
    }

    initialized = false;
    Serial.println("I2SDuplex: Shutdown complete");
}

//=============================================================================
// Output (Playback) Functions
//=============================================================================

size_t I2SDuplex::write(const int16_t* samples, size_t numSamples) {
    if (!initialized || !txHandle) {
        return 0;
    }

    size_t bytesWritten = 0;
    size_t bytesToWrite = numSamples * sizeof(int16_t);

    esp_err_t err = i2s_channel_write(txHandle, samples, bytesToWrite,
                                       &bytesWritten, portMAX_DELAY);

    if (err != ESP_OK) {
        Serial.printf("I2SDuplex: Write error: %d\n", err);
        return 0;
    }

    return bytesWritten / sizeof(int16_t);
}

bool I2SDuplex::writeSample(int16_t left, int16_t right) {
    int16_t samples[2] = { left, right };
    return write(samples, 2) == 2;
}

//=============================================================================
// Input (Microphone) Functions
//=============================================================================

size_t I2SDuplex::read(int16_t* samples, size_t numSamples) {
    if (!initialized || !rxHandle || !micEnabled) {
        return 0;
    }

    size_t bytesRead = 0;
    size_t bytesToRead = numSamples * sizeof(int16_t);

    esp_err_t err = i2s_channel_read(rxHandle, samples, bytesToRead,
                                      &bytesRead, pdMS_TO_TICKS(10));

    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        // Don't spam on timeout (normal when no data ready)
        if (err != ESP_ERR_TIMEOUT) {
            Serial.printf("I2SDuplex: Read error: %d\n", err);
        }
        return 0;
    }

    return bytesRead / sizeof(int16_t);
}

float I2SDuplex::getMicLevel() {
    if (!initialized || !rxHandle || !micEnabled) {
        return 0.0f;
    }

    // Read samples into buffer
    size_t samplesRead = read(micBuffer, MIC_BUFFER_SIZE);
    if (samplesRead == 0) {
        return currentMicLevel * 0.95f;  // Decay if no new samples
    }

    // Calculate RMS (Root Mean Square) of samples
    float sumSquares = 0.0f;
    for (size_t i = 0; i < samplesRead; i++) {
        float sample = micBuffer[i] / 32768.0f;  // Normalize to -1.0 to 1.0
        sample *= micAttenuation;  // Apply software attenuation for negative gain
        sumSquares += sample * sample;
    }

    float rms = sqrtf(sumSquares / samplesRead);

    // Smooth the level (fast attack, slow decay)
    if (rms > currentMicLevel) {
        currentMicLevel = currentMicLevel + (rms - currentMicLevel) * 0.5f;
    } else {
        currentMicLevel = currentMicLevel + (rms - currentMicLevel) * 0.1f;
    }

    return currentMicLevel;
}

void I2SDuplex::setMicEnabled(bool enable) {
    micEnabled = enable;
    if (!enable) {
        currentMicLevel = 0.0f;
    }
}

//=============================================================================
// Configuration
//=============================================================================

void I2SDuplex::setVolume(int vol) {
    volume = constrain(vol, 0, 100);
    // Volume is typically controlled via ES8311 codec, not I2S
}
