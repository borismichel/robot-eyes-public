/**
 * @file wake_word.cpp
 * @brief Wake word detection implementation using ESP-SR
 *
 * ESP-SR integration for on-device wake word detection.
 * When ESP-SR is not available, runs in stub mode with manual trigger.
 *
 * To enable ESP-SR, add to platformio.ini:
 *   lib_deps =
 *     https://github.com/espressif/esp-sr.git
 *
 * And define ESP_SR_ENABLED in your build flags.
 */

#include "wake_word.h"

// ESP-SR would be included here when available:
// #ifdef ESP_SR_ENABLED
// #include "esp_wn_iface.h"
// #include "esp_wn_models.h"
// #include "esp_afe_sr_models.h"
// #endif

//=============================================================================
// Wake Word Names
//=============================================================================

static const char* WAKE_WORD_NAMES[] = {
    "Hi ESP",       // WAKE_WORD_HI_ESP
    "Alexa",        // WAKE_WORD_ALEXA
    "Hey Buddy"     // WAKE_WORD_CUSTOM
};

//=============================================================================
// Constructor / Destructor
//=============================================================================

WakeWordDetector::WakeWordDetector()
    : initialized(false)
    , enabled(true)
    , espSrAvailable(false)
    , sensitivity(WAKE_WORD_DEFAULT_SENSITIVITY)
    , wakeWordId(WAKE_WORD_CUSTOM)
    , srHandle(nullptr)
    , frameIndex(0)
    , wakeWordCallback(nullptr)
{
    memset(frameBuffer, 0, sizeof(frameBuffer));
}

WakeWordDetector::~WakeWordDetector() {
    end();
}

//=============================================================================
// Initialization
//=============================================================================

bool WakeWordDetector::begin(int wordId) {
    if (initialized) return true;

    wakeWordId = wordId;
    Serial.printf("[WakeWord] Initializing for '%s'...\n", getWakeWordName());

#ifdef ESP_SR_ENABLED
    // Initialize ESP-SR when available
    // This requires ESP-ADF/esp-sr component to be properly configured

    /*
    // Example ESP-SR initialization:
    srmodel_list_t *models = esp_srmodel_init("model");
    if (models == NULL) {
        Serial.println("[WakeWord] Failed to load models");
        espSrAvailable = false;
    } else {
        char *wn_name = esp_srmodel_filter(models, ESP_WN_PREFIX, NULL);
        const esp_wn_iface_t *wakenet = esp_wn_handle_from_name(wn_name);

        srHandle = wakenet->create(wn_name, DET_MODE_90);
        if (srHandle) {
            espSrAvailable = true;
            Serial.println("[WakeWord] ESP-SR initialized");
        }
    }
    */

    espSrAvailable = false;  // Set to true when ESP-SR is properly configured
#else
    espSrAvailable = false;
#endif

    if (!espSrAvailable) {
        Serial.println("[WakeWord] Running in stub mode (ESP-SR not configured)");
        Serial.println("[WakeWord] Use trigger() or push-to-talk to activate assistant");
        Serial.println("[WakeWord] To enable: add esp-sr to platformio.ini and define ESP_SR_ENABLED");
    }

    initialized = true;
    return true;
}

void WakeWordDetector::end() {
    if (!initialized) return;

#ifdef ESP_SR_ENABLED
    // Cleanup ESP-SR
    /*
    if (srHandle) {
        // wakenet->destroy(srHandle);
        srHandle = nullptr;
    }
    */
#endif

    espSrAvailable = false;
    initialized = false;
    Serial.println("[WakeWord] Shutdown");
}

//=============================================================================
// Detection
//=============================================================================

bool WakeWordDetector::process(const int16_t* samples, size_t count) {
    if (!initialized || !enabled) return false;

    // Accumulate samples into frame buffer
    for (size_t i = 0; i < count; i++) {
        frameBuffer[frameIndex++] = samples[i];

        if (frameIndex >= WAKE_WORD_FRAME_SIZE) {
            if (processFrame()) {
                return true;
            }
            frameIndex = 0;
        }
    }

    return false;
}

bool WakeWordDetector::processFrame() {
    if (!espSrAvailable) {
        // Stub mode - no actual detection
        return false;
    }

#ifdef ESP_SR_ENABLED
    // Process frame with ESP-SR when available
    /*
    int result = wakenet->detect(srHandle, frameBuffer);
    if (result > 0) {
        Serial.printf("[WakeWord] '%s' detected!\n", getWakeWordName());

        if (wakeWordCallback) {
            wakeWordCallback();
        }
        return true;
    }
    */
#endif

    return false;
}

void WakeWordDetector::trigger() {
    if (!initialized || !enabled) return;

    Serial.printf("[WakeWord] Manual trigger! ('%s')\n", getWakeWordName());

    if (wakeWordCallback) {
        wakeWordCallback();
    }
}

//=============================================================================
// Configuration
//=============================================================================

void WakeWordDetector::setSensitivity(float sens) {
    sensitivity = constrain(sens, 0.0f, 1.0f);

#ifdef ESP_SR_ENABLED
    // Update ESP-SR sensitivity when available
    /*
    if (srHandle) {
        wakenet->set_det_threshold(srHandle, sensitivity);
    }
    */
#endif
}

const char* WakeWordDetector::getWakeWordName() const {
    if (wakeWordId >= 0 && wakeWordId < 3) {
        return WAKE_WORD_NAMES[wakeWordId];
    }
    return "Unknown";
}
