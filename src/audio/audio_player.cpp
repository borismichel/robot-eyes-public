/**
 * @file audio_player.cpp
 * @brief Audio playback implementation using full-duplex I2S
 *
 * Supports two playback modes:
 * - File-based: MP3/WAV decoding via ESP8266Audio library
 * - Streaming PCM: Direct I2S feed for TTS audio (bypasses AudioGenerator)
 *
 * Uses a custom AudioOutput that shares the I2S bus with microphone input
 * (full-duplex operation). Runs on a dedicated FreeRTOS task on Core 0
 * for smooth playback while the main display loop runs on Core 1.
 *
 * @author Robot Eyes Project
 * @date 2026
 */

#include "audio_player.h"
#include "audio_output_duplex.h"
#include "i2s_duplex.h"
#include "pin_config.h"
#include <LittleFS.h>
#include <Wire.h>
#include "es8311.h"

// ESP8266Audio includes
#include <AudioGenerator.h>
#include <AudioGeneratorMP3.h>
#include <AudioGeneratorWAV.h>
#include <AudioFileSourceLittleFS.h>
#include <AudioFileSourcePROGMEM.h>

//=============================================================================
// Static Variables
//=============================================================================

/** ES8311 codec handle */
static es8311_handle_t es8311Handle = nullptr;

/** Audio task handle */
static TaskHandle_t audioTaskHandle = nullptr;

/** Pointer to AudioPlayer instance for task callback */
static AudioPlayer* audioPlayerInstance = nullptr;

//=============================================================================
// Audio Task
//=============================================================================

/**
 * @brief FreeRTOS task for audio playback
 *
 * Runs on Core 0 to avoid blocking the display loop on Core 1.
 * During playback, runs a tight decode loop — the blocking I2S write
 * inside ConsumeSample provides natural yielding. When idle, sleeps
 * to save CPU.
 */
void audioTask(void* parameter) {
    AudioPlayer* player = (AudioPlayer*)parameter;
    while (true) {
        player->taskUpdate();
        if (!player->isPlaying() || player->isStreamingPCM()) {
            vTaskDelay(pdMS_TO_TICKS(10));  // Idle or streaming: yield to avoid starving IDLE0
        }
        // During file playback: no delay — blocking I2S write yields naturally
    }
}

//=============================================================================
// Constructor / Destructor
//=============================================================================

AudioPlayer::AudioPlayer()
    : initialized(false)
    , volume(80)
    , micAttenuation(1.0f)  // 0dB attenuation by default
    , generator(nullptr)
    , file(nullptr)
    , out(nullptr)
    , psramBuffer(nullptr)
    , psramBufferSize(0)
    , streamingPCM(false)
    , streamChannels(1)
    , streamTrailingByte(-1)
    , taskRunning(false)
    , audioMutex(nullptr) {
    // Create mutex for thread-safe access to mp3/file between cores
    audioMutex = xSemaphoreCreateMutex();
}

AudioPlayer::~AudioPlayer() {
    // Delete the audio task
    if (audioTaskHandle) {
        vTaskDelete(audioTaskHandle);
        audioTaskHandle = nullptr;
    }

    stop();

    delete generator;
    delete file;
    delete out;
    freePsramBuffer();

    // Delete the mutex
    if (audioMutex) {
        vSemaphoreDelete(audioMutex);
        audioMutex = nullptr;
    }
}

//=============================================================================
// Initialization
//=============================================================================

/**
 * @brief Initialize the ES8311 audio codec
 *
 * Configures the codec for playback at 44.1kHz with 16-bit resolution.
 * Also enables the ADC for microphone input in full-duplex mode.
 */
bool AudioPlayer::initCodec() {
    // Create ES8311 handle
    es8311Handle = es8311_create(0, ES8311_ADDRESS_0);
    if (!es8311Handle) {
        Serial.println("AudioPlayer: Failed to create ES8311 handle");
        return false;
    }

    // Configure clock - use MCLK pin
    es8311_clock_config_t clk_cfg = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = 44100 * 256,  // 11.2896 MHz for 44.1kHz
        .sample_frequency = 44100
    };

    esp_err_t err = es8311_init(es8311Handle, &clk_cfg, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16);
    if (err != ESP_OK) {
        Serial.printf("AudioPlayer: ES8311 init failed: %d\n", err);
        return false;
    }

    // Configure codec for BOTH playback AND recording (full-duplex)
    // Enable microphone with conservative gain for speech capture
    es8311_microphone_config(es8311Handle, false);  // false = analog mic
    es8311_microphone_gain_set(es8311Handle, ES8311_MIC_GAIN_0DB);  // 0dB digital gain — ADC volume set by setMicGain()

    // Dump ES8311 registers for debugging audio issues
    Serial.println("AudioPlayer: ES8311 register dump after init:");
    es8311_register_dump(es8311Handle);

    // Set output volume
    es8311_voice_volume_set(es8311Handle, volume, nullptr);

    Serial.println("AudioPlayer: ES8311 codec initialized (full-duplex)");
    return true;
}

/**
 * @brief Initialize the audio player
 *
 * Sets up:
 * - LittleFS filesystem for audio files
 * - Power amplifier
 * - ES8311 codec
 * - Full-duplex I2S driver
 * - MP3 decoder
 * - Background playback task
 */
bool AudioPlayer::begin() {
    // Initialize LittleFS for audio file storage
    if (!LittleFS.begin(true)) {
        Serial.println("AudioPlayer: LittleFS mount failed");
        return false;
    }
    Serial.println("AudioPlayer: LittleFS mounted");

    // Enable power amplifier for speaker output
    pinMode(PA, OUTPUT);
    digitalWrite(PA, HIGH);

    // Initialize the ES8311 codec
    if (!initCodec()) {
        return false;
    }

    // Initialize the full-duplex I2S driver
    I2SDuplex& i2s = I2SDuplex::getInstance();
    if (!i2s.begin(44100)) {
        Serial.println("AudioPlayer: I2S duplex init failed");
        return false;
    }

    // Create audio output using our duplex driver
    out = new AudioOutputDuplex();
    if (!out->begin()) {
        Serial.println("AudioPlayer: AudioOutputDuplex init failed");
        return false;
    }
    out->SetGain(volume / 100.0f);

    // Generator created per-file in play() based on extension

    // Start audio task on Core 0 (display/main loop runs on Core 1)
    audioPlayerInstance = this;
    xTaskCreatePinnedToCore(
        audioTask,          // Task function
        "AudioTask",        // Task name
        8192,               // Stack size (bytes)
        this,               // Parameter
        10,                 // Priority (high enough to avoid WiFi preemption gaps)
        &audioTaskHandle,   // Task handle
        0                   // Core 0
    );

    initialized = true;
    taskRunning = true;
    Serial.println("AudioPlayer: Initialized with full-duplex I2S on core 0");
    return true;
}

//=============================================================================
// Playback Control
//=============================================================================

/**
 * @brief Play an audio file from LittleFS
 *
 * Reads the entire file into PSRAM first, then decodes from memory.
 * This eliminates SPI flash contention during playback, which was
 * causing choppy audio on longer files.
 *
 * @param filename Path to audio file (e.g., "/happy.mp3")
 * @return true if playback started successfully
 */
bool AudioPlayer::play(const char* filename) {
    if (!initialized) {
        Serial.println("AudioPlayer: Not initialized");
        return false;
    }

    // Acquire mutex to safely access generator/file (shared with audio task on Core 0)
    if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("AudioPlayer: Failed to acquire mutex for play");
        return false;
    }

    // Stop any current playback (mutex already held)
    if (generator && generator->isRunning()) {
        generator->stop();
    }
    delete generator;
    generator = nullptr;
    if (file) {
        delete file;
        file = nullptr;
    }
    freePsramBuffer();

    // Read entire file into PSRAM to eliminate SPI flash contention during decode
    File f = LittleFS.open(filename, "r");
    if (!f) {
        Serial.printf("AudioPlayer: Failed to open %s\n", filename);
        xSemaphoreGive(audioMutex);
        return false;
    }

    psramBufferSize = f.size();
    if (psramBufferSize == 0) {
        Serial.printf("AudioPlayer: Empty file %s\n", filename);
        f.close();
        xSemaphoreGive(audioMutex);
        return false;
    }

    // Debug: dump first 16 bytes to verify file header
    {
        uint8_t hdr[16];
        size_t hdrRead = f.read(hdr, 16);
        Serial.printf("AudioPlayer: First %u bytes:", hdrRead);
        for (size_t i = 0; i < hdrRead; i++) Serial.printf(" %02X", hdr[i]);
        Serial.println();
        f.seek(0);  // Reset to beginning
    }

    // WAV decoder is lightweight — read directly from LittleFS (no PSRAM needed).
    // MP3 decoder is heavy — buffer in PSRAM to avoid flash contention.
    String fname(filename);
    bool isWav = fname.endsWith(".wav");

    if (!isWav) {
        psramBuffer = (uint8_t*)ps_malloc(psramBufferSize);
        if (psramBuffer) {
            size_t bytesRead = f.read(psramBuffer, psramBufferSize);
            f.close();
            Serial.printf("AudioPlayer: Buffered %u bytes in PSRAM\n", bytesRead);
            file = new AudioFileSourcePROGMEM(psramBuffer, psramBufferSize);
        } else {
            Serial.printf("AudioPlayer: PSRAM alloc failed (%u bytes), using flash\n", psramBufferSize);
            psramBufferSize = 0;
            f.close();
            file = new AudioFileSourceLittleFS(filename);
        }
    } else {
        f.close();
        file = new AudioFileSourceLittleFS(filename);
    }

    if (!file->isOpen()) {
        Serial.printf("AudioPlayer: Failed to open %s\n", filename);
        delete file;
        file = nullptr;
        freePsramBuffer();
        xSemaphoreGive(audioMutex);
        return false;
    }

    // Pick decoder based on file extension
    if (isWav) {
        generator = new AudioGeneratorWAV();
    } else {
        generator = new AudioGeneratorMP3();
    }

    // Start playback
    if (!generator->begin(file, out)) {
        Serial.printf("AudioPlayer: Failed to start playback for %s\n", filename);
        delete generator;
        generator = nullptr;
        delete file;
        file = nullptr;
        freePsramBuffer();
        xSemaphoreGive(audioMutex);
        return false;
    }

    xSemaphoreGive(audioMutex);
    Serial.printf("AudioPlayer: Playing %s\n", filename);
    return true;
}

/**
 * @brief Stop current playback
 */
void AudioPlayer::stop() {
    // Acquire mutex to safely access mp3/file
    if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("AudioPlayer: Failed to acquire mutex for stop");
        return;
    }

    if (generator && generator->isRunning()) {
        generator->stop();
    }
    delete generator;
    generator = nullptr;
    if (file) {
        delete file;
        file = nullptr;
    }
    freePsramBuffer();

    xSemaphoreGive(audioMutex);
}

void AudioPlayer::freePsramBuffer() {
    if (psramBuffer) {
        free(psramBuffer);
        psramBuffer = nullptr;
        psramBufferSize = 0;
    }
}

/**
 * @brief Check if currently playing
 */
bool AudioPlayer::isPlaying() const {
    return (generator && generator->isRunning()) || streamingPCM;
}

/**
 * @brief Update function (no-op, audio runs on separate task)
 */
void AudioPlayer::update() {
    // No-op - audio runs on separate task now
}

/**
 * @brief Internal update called from audio task
 *
 * Decodes multiple MP3 frames per call to reduce mutex overhead.
 * The blocking I2S write inside ConsumeSample provides natural pacing —
 * once the DMA buffer is full, each frame blocks for ~48ms until space
 * frees up. This lets the decoder stay ahead of playback and tolerate
 * brief CPU stalls from WiFi or other tasks.
 */
void AudioPlayer::taskUpdate() {
    // Try to acquire mutex (non-blocking to avoid stalling audio task)
    if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;  // Main thread is using mp3/file, skip this iteration
    }

    if (generator && generator->isRunning()) {
        // Decode multiple frames per mutex hold to reduce overhead.
        // Blocking I2S writes inside loop() provide natural pacing.
        for (int i = 0; i < 4; i++) {
            if (!generator->loop()) {
                // Playback finished — flush remaining buffered samples
                ((AudioOutputDuplex*)out)->flush();
                generator->stop();
                delete generator;
                generator = nullptr;
                if (file) {
                    delete file;
                    file = nullptr;
                }
                freePsramBuffer();
                Serial.println("AudioPlayer: Playback finished");
                break;
            }
        }
        // Flush any partial buffer from the last frame
        if (generator) {
            ((AudioOutputDuplex*)out)->flush();
        }
    }

    xSemaphoreGive(audioMutex);
}

//=============================================================================
// Volume Control
//=============================================================================

/**
 * @brief Set playback volume
 * @param vol Volume level 0-100
 */
void AudioPlayer::setVolume(int vol) {
    volume = constrain(vol, 0, 100);

    // Update output gain
    if (out) {
        ((AudioOutputDuplex*)out)->SetGain(volume / 100.0f);
    }

    // Update codec volume
    if (es8311Handle) {
        es8311_voice_volume_set(es8311Handle, volume, nullptr);
    }
}

//=============================================================================
// Streaming PCM (direct to I2S, bypasses AudioGenerator)
//=============================================================================

bool AudioPlayer::startStreamingPCM(int sampleRate, int channels, int bitsPerSample) {
    if (!initialized || !out) return false;

    // Stop any file-based playback
    stop();

    streamChannels = channels;
    streamTrailingByte = -1;

    // Re-enable the output (previous file-based playback may have called stop())
    ((AudioOutputDuplex*)out)->begin();

    // Set source sample rate — ConsumeSample handles Bresenham resampling to 44.1kHz
    ((AudioOutputDuplex*)out)->SetRate(sampleRate);

    streamingPCM = true;
    Serial.printf("AudioPlayer: Streaming PCM %dHz %dch %dbit\n", sampleRate, channels, bitsPerSample);
    return true;
}

void AudioPlayer::feedPCMBytes(const uint8_t* data, size_t length) {
    if (!streamingPCM || !out) return;

    size_t offset = 0;

    // Handle leftover byte from previous chunk
    if (streamTrailingByte >= 0 && length > 0) {
        uint8_t pair[2] = { (uint8_t)streamTrailingByte, data[0] };
        int16_t sample;
        memcpy(&sample, pair, 2);
        int16_t stereo[2] = { sample, sample };
        out->ConsumeSample(stereo);
        offset = 1;
        streamTrailingByte = -1;
    }

    // Bytes per frame (mono=2, stereo=4 for 16-bit)
    size_t frameBytes = streamChannels * 2;

    while ((length - offset) >= frameBytes) {
        int16_t stereo[2];
        if (streamChannels == 1) {
            int16_t sample;
            memcpy(&sample, data + offset, 2);
            stereo[0] = sample;
            stereo[1] = sample;
        } else {
            memcpy(&stereo[0], data + offset, 2);
            memcpy(&stereo[1], data + offset + 2, 2);
        }
        out->ConsumeSample(stereo);
        offset += frameBytes;
    }

    // Save any trailing byte (mono: 1 leftover byte possible)
    if (length - offset == 1) {
        streamTrailingByte = data[offset];
    }
}

void AudioPlayer::finishStreamingPCM() {
    if (!streamingPCM) return;
    ((AudioOutputDuplex*)out)->flush();
    streamingPCM = false;
    streamTrailingByte = -1;
    Serial.println("AudioPlayer: Streaming PCM finished");
}

//=============================================================================
// Mic Gain Control
//=============================================================================

void AudioPlayer::setMicGain(int sliderValue) {
    if (!es8311Handle) return;

    // The ES8311 mic gain chain:
    //   Analog PGA (REG14, fixed at init) → ADC → ADC Volume (REG17) → Digital Gain (REG16) → I2S
    //
    // Slider mapping (all hardware, no software attenuation needed):
    //   0-50:   ADC volume (REG17) from 0x00 to 0xA8, digital gain 0dB
    //   50-100: ADC volume at 0xA8, digital gain (REG16) from 0dB to +42dB

    micAttenuation = 1.0f;  // No software attenuation — all gain control in hardware

    if (sliderValue <= 50) {
        // Map 0-50 to ADC volume register 0x00-0xA8
        uint8_t adcVol = (uint8_t)((sliderValue / 50.0f) * 0xA8);
        es8311_microphone_volume_set(es8311Handle, adcVol);
        es8311_microphone_gain_set(es8311Handle, ES8311_MIC_GAIN_0DB);

        Serial.printf("Mic gain: slider=%d, REG17=0x%02X, REG16=0dB\n", sliderValue, adcVol);
    } else {
        // Keep ADC volume at max useful level, add digital gain
        es8311_microphone_volume_set(es8311Handle, 0xA8);

        int gainRange = sliderValue - 50;  // 0-50
        es8311_mic_gain_t gain;
        int gainDb;

        if (gainRange < 7) {
            gain = ES8311_MIC_GAIN_0DB; gainDb = 0;
        } else if (gainRange < 14) {
            gain = ES8311_MIC_GAIN_6DB; gainDb = 6;
        } else if (gainRange < 21) {
            gain = ES8311_MIC_GAIN_12DB; gainDb = 12;
        } else if (gainRange < 28) {
            gain = ES8311_MIC_GAIN_18DB; gainDb = 18;
        } else if (gainRange < 35) {
            gain = ES8311_MIC_GAIN_24DB; gainDb = 24;
        } else if (gainRange < 42) {
            gain = ES8311_MIC_GAIN_30DB; gainDb = 30;
        } else if (gainRange < 49) {
            gain = ES8311_MIC_GAIN_36DB; gainDb = 36;
        } else {
            gain = ES8311_MIC_GAIN_42DB; gainDb = 42;
        }

        es8311_microphone_gain_set(es8311Handle, gain);
        Serial.printf("Mic gain: slider=%d, REG17=0xA8, REG16=+%ddB\n", sliderValue, gainDb);
    }

    // Update I2SDuplex with attenuation factor (1.0 since all gain is in hardware)
    I2SDuplex& i2s = I2SDuplex::getInstance();
    i2s.setMicAttenuation(micAttenuation);
}
