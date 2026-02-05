/**
 * @file audio_player.cpp
 * @brief MP3 audio playback implementation using full-duplex I2S
 *
 * Uses ESP8266Audio library for MP3 decoding with a custom AudioOutput
 * that shares the I2S bus with microphone input (full-duplex operation).
 *
 * Runs on a dedicated FreeRTOS task on Core 0 for smooth playback while
 * the main display loop runs on Core 1.
 *
 * @author Robot Eyes Project
 * @date 2025
 */

#include "audio_player.h"
#include "audio_output_duplex.h"
#include "i2s_duplex.h"
#include "pin_config.h"
#include <LittleFS.h>
#include <Wire.h>
#include "es8311.h"

// ESP8266Audio includes
#include <AudioGeneratorMP3.h>
#include <AudioFileSourceLittleFS.h>

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
 * Continuously feeds the MP3 decoder while playback is active.
 */
void audioTask(void* parameter) {
    AudioPlayer* player = (AudioPlayer*)parameter;
    while (true) {
        player->taskUpdate();
        vTaskDelay(1);  // Yield to other tasks, ~1ms delay
    }
}

//=============================================================================
// Constructor / Destructor
//=============================================================================

AudioPlayer::AudioPlayer()
    : initialized(false)
    , volume(80)
    , micAttenuation(1.0f)  // 0dB attenuation by default
    , mp3(nullptr)
    , file(nullptr)
    , out(nullptr)
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

    delete mp3;
    delete file;
    delete out;

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
    // Enable microphone with moderate gain for clap detection
    es8311_microphone_config(es8311Handle, false);  // false = analog mic
    es8311_microphone_gain_set(es8311Handle, ES8311_MIC_GAIN_18DB);  // 18dB gain for loud sound detection

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

    // Create MP3 decoder
    mp3 = new AudioGeneratorMP3();

    // Start audio task on Core 0 (display/main loop runs on Core 1)
    audioPlayerInstance = this;
    xTaskCreatePinnedToCore(
        audioTask,          // Task function
        "AudioTask",        // Task name
        8192,               // Stack size (bytes)
        this,               // Parameter
        1,                  // Priority
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
 * @brief Play an MP3 file from LittleFS
 * @param filename Path to MP3 file (e.g., "/happy.mp3")
 * @return true if playback started successfully
 */
bool AudioPlayer::play(const char* filename) {
    if (!initialized) {
        Serial.println("AudioPlayer: Not initialized");
        return false;
    }

    // Acquire mutex to safely access mp3/file (shared with audio task on Core 0)
    if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        Serial.println("AudioPlayer: Failed to acquire mutex for play");
        return false;
    }

    // Stop any current playback (mutex already held)
    if (mp3 && mp3->isRunning()) {
        mp3->stop();
    }
    if (file) {
        delete file;
        file = nullptr;
    }

    // Create new file source
    file = new AudioFileSourceLittleFS(filename);
    if (!file->isOpen()) {
        Serial.printf("AudioPlayer: Failed to open %s\n", filename);
        delete file;
        file = nullptr;
        xSemaphoreGive(audioMutex);
        return false;
    }

    // Start playback
    if (!mp3->begin(file, out)) {
        Serial.printf("AudioPlayer: Failed to start MP3 playback for %s\n", filename);
        delete file;
        file = nullptr;
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

    if (mp3 && mp3->isRunning()) {
        mp3->stop();
    }
    if (file) {
        delete file;
        file = nullptr;
    }

    xSemaphoreGive(audioMutex);
}

/**
 * @brief Check if currently playing
 */
bool AudioPlayer::isPlaying() const {
    return mp3 && mp3->isRunning();
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
 * Feeds the MP3 decoder until playback completes.
 * Uses mutex to safely access mp3/file shared with main thread.
 */
void AudioPlayer::taskUpdate() {
    // Try to acquire mutex (non-blocking to avoid stalling audio task)
    if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return;  // Main thread is using mp3/file, skip this iteration
    }

    if (mp3 && mp3->isRunning()) {
        if (!mp3->loop()) {
            // Playback finished
            mp3->stop();
            if (file) {
                delete file;
                file = nullptr;
            }
            Serial.println("AudioPlayer: Playback finished");
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

void AudioPlayer::setMicGain(int sliderValue) {
    if (!es8311Handle) return;

    // Center-zero slider mapping:
    // - Slider 50 = 0dB (no gain, no attenuation)
    // - Slider 0-50 = -24dB to 0dB (software attenuation)
    // - Slider 50-100 = 0dB to +42dB (hardware gain)

    es8311_mic_gain_t gain;
    int gainDb = 0;

    if (sliderValue < 50) {
        // Left side: attenuation -24dB to 0dB
        // Map 0-50 to attenuation 0.0625 (1/16 = -24dB) to 1.0 (0dB)
        float t = sliderValue / 50.0f;  // 0.0 to 1.0
        micAttenuation = 0.0625f + t * (1.0f - 0.0625f);
        gain = ES8311_MIC_GAIN_0DB;  // Use 0dB hardware gain when attenuating
        gainDb = 0;

        // Calculate attenuation in dB for display
        float attenDb = 20.0f * log10f(micAttenuation);
        Serial.printf("Mic gain: %+.1fdB (slider=%d, attenuation=%.3f)\n", attenDb, sliderValue, micAttenuation);
    } else {
        // Right side: positive gain 0dB to +42dB
        // No software attenuation
        micAttenuation = 1.0f;

        // Map 50-100 to gain 0-42dB (8 levels)
        int gainRange = sliderValue - 50;  // 0-50

        if (gainRange < 7) {
            gain = ES8311_MIC_GAIN_0DB;
            gainDb = 0;
        } else if (gainRange < 14) {
            gain = ES8311_MIC_GAIN_6DB;
            gainDb = 6;
        } else if (gainRange < 21) {
            gain = ES8311_MIC_GAIN_12DB;
            gainDb = 12;
        } else if (gainRange < 28) {
            gain = ES8311_MIC_GAIN_18DB;
            gainDb = 18;
        } else if (gainRange < 35) {
            gain = ES8311_MIC_GAIN_24DB;
            gainDb = 24;
        } else if (gainRange < 42) {
            gain = ES8311_MIC_GAIN_30DB;
            gainDb = 30;
        } else if (gainRange < 49) {
            gain = ES8311_MIC_GAIN_36DB;
            gainDb = 36;
        } else {
            gain = ES8311_MIC_GAIN_42DB;
            gainDb = 42;
        }

        Serial.printf("Mic gain: +%ddB (slider=%d)\n", gainDb, sliderValue);
    }

    es8311_microphone_gain_set(es8311Handle, gain);

    // Update I2SDuplex with attenuation factor
    I2SDuplex& i2s = I2SDuplex::getInstance();
    i2s.setMicAttenuation(micAttenuation);
}
