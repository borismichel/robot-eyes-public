/**
 * @file ota_manager.cpp
 * @brief OTA firmware update manager implementation
 */

#include "ota_manager.h"
#include "version.h"
#include <esp_app_format.h>
#include <Preferences.h>

// ESP32 image magic byte
#define ESP_IMAGE_HEADER_MAGIC 0xE9

// NVS namespace for OTA settings
#define OTA_NVS_NAMESPACE "ota"
#define OTA_NVS_KEY "sigkey"

OtaManager::OtaManager()
    : state(OtaState::Idle)
    , otaHandle(0)
    , updatePartition(nullptr)
    , runningPartition(nullptr)
    , bytesReceived(0)
    , totalBytes(0)
    , firmwareSize(0)
    , headerValidated(false)
    , signingKeySet(false)
    , signatureBufferPos(0) {
    errorMessage[0] = '\0';
    memset(signingKey, 0, sizeof(signingKey));
    memset(receivedSignature, 0, sizeof(receivedSignature));
    memset(signatureBuffer, 0, sizeof(signatureBuffer));
    mbedtls_md_init(&hmacCtx);
}

void OtaManager::begin() {
    // Get running partition info
    runningPartition = esp_ota_get_running_partition();

    if (runningPartition) {
        Serial.printf("[OTA] Running partition: %s\n", runningPartition->label);
    }

    // Check if we need to validate this boot (after OTA update)
    esp_ota_img_states_t otaState;
    if (esp_ota_get_state_partition(runningPartition, &otaState) == ESP_OK) {
        if (otaState == ESP_OTA_IMG_PENDING_VERIFY) {
            Serial.println("[OTA] Validating new firmware...");
            // Mark as valid - in production, you'd run diagnostics first
            esp_ota_mark_app_valid_cancel_rollback();
            Serial.println("[OTA] Firmware validated successfully");
        }
    }

    // Find the update partition
    updatePartition = esp_ota_get_next_update_partition(nullptr);
    if (updatePartition) {
        Serial.printf("[OTA] Update partition: %s (size: %lu bytes)\n",
                      updatePartition->label, updatePartition->size);
    }

    // Load signing key from NVS
    loadSigningKey();

    Serial.printf("[OTA] Version: %s, Built: %s\n", FIRMWARE_VERSION, FIRMWARE_BUILD_DATE);
    Serial.printf("[OTA] Signature verification: %s\n", signingKeySet ? "enabled" : "disabled");
}

bool OtaManager::startUpload(size_t size) {
    if (state == OtaState::Uploading) {
        setError("Upload already in progress");
        return false;
    }

    if (!updatePartition) {
        setError("No update partition found");
        state = OtaState::Error;
        return false;
    }

    // If signing key is set, the last 32 bytes are the signature
    firmwareSize = signingKeySet ? (size - OTA_SIGNATURE_SIZE) : size;

    if (firmwareSize > updatePartition->size) {
        setError("Firmware too large for partition");
        state = OtaState::Error;
        return false;
    }

    if (signingKeySet && size <= OTA_SIGNATURE_SIZE) {
        setError("File too small (no firmware data)");
        state = OtaState::Error;
        return false;
    }

    // Begin OTA with firmware size (excluding signature)
    esp_err_t err = esp_ota_begin(updatePartition, firmwareSize, &otaHandle);
    if (err != ESP_OK) {
        snprintf(errorMessage, sizeof(errorMessage), "OTA begin failed: %s", esp_err_to_name(err));
        state = OtaState::Error;
        return false;
    }

    totalBytes = size;
    bytesReceived = 0;
    headerValidated = false;
    signatureBufferPos = 0;
    memset(signatureBuffer, 0, sizeof(signatureBuffer));
    state = OtaState::Uploading;
    errorMessage[0] = '\0';

    // Initialize HMAC context if signing key is set
    if (signingKeySet) {
        mbedtls_md_free(&hmacCtx);
        mbedtls_md_init(&hmacCtx);
        mbedtls_md_setup(&hmacCtx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
        mbedtls_md_hmac_starts(&hmacCtx, signingKey, sizeof(signingKey));
    }

    Serial.printf("[OTA] Upload started, expecting %lu bytes (firmware: %lu, signature: %s)\n",
                  totalBytes, firmwareSize, signingKeySet ? "yes" : "no");
    return true;
}

bool OtaManager::writeChunk(const uint8_t* data, size_t length) {
    if (state != OtaState::Uploading) {
        setError("No upload in progress");
        return false;
    }

    // Validate header on first chunk
    if (!headerValidated && bytesReceived == 0) {
        if (!validateImageHeader(data, length)) {
            cancelUpload();
            return false;
        }
        headerValidated = true;
    }

    size_t dataToProcess = length;
    size_t dataOffset = 0;

    if (signingKeySet) {
        // Calculate how much of this chunk is firmware vs signature
        size_t totalAfterThisChunk = bytesReceived + length;

        if (totalAfterThisChunk > firmwareSize) {
            // Some or all of this chunk is signature data
            size_t firmwareInChunk = 0;
            if (bytesReceived < firmwareSize) {
                firmwareInChunk = firmwareSize - bytesReceived;
            }

            // Write firmware portion to flash and update HMAC
            if (firmwareInChunk > 0) {
                esp_err_t err = esp_ota_write(otaHandle, data, firmwareInChunk);
                if (err != ESP_OK) {
                    snprintf(errorMessage, sizeof(errorMessage), "Write failed: %s", esp_err_to_name(err));
                    state = OtaState::Error;
                    esp_ota_abort(otaHandle);
                    return false;
                }
                mbedtls_md_hmac_update(&hmacCtx, data, firmwareInChunk);
            }

            // Copy signature portion to buffer
            size_t signatureInChunk = length - firmwareInChunk;
            const uint8_t* sigData = data + firmwareInChunk;

            for (size_t i = 0; i < signatureInChunk && signatureBufferPos < OTA_SIGNATURE_SIZE; i++) {
                receivedSignature[signatureBufferPos++] = sigData[i];
            }

            bytesReceived += length;
            return true;
        }

        // All data is firmware
        esp_err_t err = esp_ota_write(otaHandle, data, length);
        if (err != ESP_OK) {
            snprintf(errorMessage, sizeof(errorMessage), "Write failed: %s", esp_err_to_name(err));
            state = OtaState::Error;
            esp_ota_abort(otaHandle);
            return false;
        }
        mbedtls_md_hmac_update(&hmacCtx, data, length);
        bytesReceived += length;
        return true;
    }

    // No signature verification - write everything
    esp_err_t err = esp_ota_write(otaHandle, data, length);
    if (err != ESP_OK) {
        snprintf(errorMessage, sizeof(errorMessage), "Write failed: %s", esp_err_to_name(err));
        state = OtaState::Error;
        esp_ota_abort(otaHandle);
        return false;
    }

    bytesReceived += length;
    return true;
}

bool OtaManager::finishUpload() {
    if (state != OtaState::Uploading) {
        setError("No upload to finish");
        return false;
    }

    state = OtaState::Verifying;
    Serial.println("[OTA] Verifying firmware...");

    // Verify signature if key is set
    if (signingKeySet) {
        if (!verifySignature()) {
            esp_ota_abort(otaHandle);
            return false;
        }
        Serial.println("[OTA] Signature verified");
    }

    // End OTA - this verifies the image
    esp_err_t err = esp_ota_end(otaHandle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            setError("Firmware validation failed");
        } else {
            snprintf(errorMessage, sizeof(errorMessage), "OTA end failed: %s", esp_err_to_name(err));
        }
        state = OtaState::Error;
        return false;
    }

    state = OtaState::Installing;
    Serial.println("[OTA] Setting boot partition...");

    // Set boot partition
    err = esp_ota_set_boot_partition(updatePartition);
    if (err != ESP_OK) {
        snprintf(errorMessage, sizeof(errorMessage), "Set boot failed: %s", esp_err_to_name(err));
        state = OtaState::Error;
        return false;
    }

    state = OtaState::Success;
    Serial.println("[OTA] Update complete! Restart to apply.");
    return true;
}

void OtaManager::cancelUpload() {
    if (state == OtaState::Uploading && otaHandle != 0) {
        esp_ota_abort(otaHandle);
        Serial.println("[OTA] Upload cancelled");
    }
    reset();
}

const char* OtaManager::getStateString() const {
    switch (state) {
        case OtaState::Idle: return "idle";
        case OtaState::Uploading: return "uploading";
        case OtaState::Verifying: return "verifying";
        case OtaState::Installing: return "installing";
        case OtaState::Success: return "success";
        case OtaState::Error: return "error";
        default: return "unknown";
    }
}

int OtaManager::getProgress() const {
    if (totalBytes == 0) return 0;
    return (int)((bytesReceived * 100) / totalBytes);
}

const char* OtaManager::getVersion() {
    return FIRMWARE_VERSION;
}

const char* OtaManager::getBuildDate() {
    return FIRMWARE_BUILD_DATE;
}

const char* OtaManager::getPartitionLabel() const {
    return runningPartition ? runningPartition->label : "unknown";
}

size_t OtaManager::getOtaPartitionSize() const {
    return updatePartition ? updatePartition->size : 0;
}

bool OtaManager::canRollback() const {
    const esp_partition_t* otherPartition = esp_ota_get_next_update_partition(runningPartition);
    if (!otherPartition) return false;

    esp_ota_img_states_t otaState;
    if (esp_ota_get_state_partition(otherPartition, &otaState) != ESP_OK) {
        return false;
    }

    // Can rollback if there's a valid image in the other partition
    return otaState == ESP_OTA_IMG_VALID || otaState == ESP_OTA_IMG_UNDEFINED;
}

bool OtaManager::rollback() {
    if (!canRollback()) {
        setError("No valid partition to rollback to");
        state = OtaState::Error;
        return false;
    }

    const esp_partition_t* otherPartition = esp_ota_get_next_update_partition(runningPartition);
    esp_err_t err = esp_ota_set_boot_partition(otherPartition);
    if (err != ESP_OK) {
        snprintf(errorMessage, sizeof(errorMessage), "Rollback failed: %s", esp_err_to_name(err));
        state = OtaState::Error;
        return false;
    }

    Serial.println("[OTA] Rollback set, restarting...");
    delay(100);
    ESP.restart();
    return true;  // Never reached
}

void OtaManager::restart() {
    Serial.println("[OTA] Restarting...");
    delay(100);
    ESP.restart();
}

bool OtaManager::validateImageHeader(const uint8_t* data, size_t length) {
    if (length < sizeof(esp_image_header_t)) {
        setError("Data too small for header");
        state = OtaState::Error;
        return false;
    }

    const esp_image_header_t* header = (const esp_image_header_t*)data;

    // Check magic byte
    if (header->magic != ESP_IMAGE_HEADER_MAGIC) {
        setError("Invalid firmware file (bad magic)");
        state = OtaState::Error;
        return false;
    }

    Serial.println("[OTA] Firmware header validated");
    return true;
}

void OtaManager::setError(const char* msg) {
    strncpy(errorMessage, msg, sizeof(errorMessage) - 1);
    errorMessage[sizeof(errorMessage) - 1] = '\0';
    Serial.printf("[OTA] Error: %s\n", msg);
}

void OtaManager::reset() {
    state = OtaState::Idle;
    otaHandle = 0;
    bytesReceived = 0;
    totalBytes = 0;
    firmwareSize = 0;
    headerValidated = false;
    signatureBufferPos = 0;
    errorMessage[0] = '\0';
}

bool OtaManager::verifySignature() {
    if (!signingKeySet) {
        return true;  // No verification needed
    }

    // Finalize HMAC
    uint8_t computedSignature[OTA_SIGNATURE_SIZE];
    mbedtls_md_hmac_finish(&hmacCtx, computedSignature);

    // Compare signatures (constant-time comparison)
    int diff = 0;
    for (int i = 0; i < OTA_SIGNATURE_SIZE; i++) {
        diff |= computedSignature[i] ^ receivedSignature[i];
    }

    if (diff != 0) {
        setError("Invalid firmware signature");
        state = OtaState::Error;
        return false;
    }

    return true;
}

bool OtaManager::setSigningKey(const uint8_t* key, size_t keyLen) {
    if (keyLen != 32) {
        Serial.println("[OTA] Invalid key length (must be 32 bytes)");
        return false;
    }

    memcpy(signingKey, key, 32);
    signingKeySet = true;
    saveSigningKey();
    Serial.println("[OTA] Signing key set");
    return true;
}

void OtaManager::clearSigningKey() {
    memset(signingKey, 0, sizeof(signingKey));
    signingKeySet = false;

    Preferences prefs;
    if (prefs.begin(OTA_NVS_NAMESPACE, false)) {
        prefs.remove(OTA_NVS_KEY);
        prefs.end();
    }
    Serial.println("[OTA] Signing key cleared");
}

void OtaManager::loadSigningKey() {
    Preferences prefs;
    if (prefs.begin(OTA_NVS_NAMESPACE, true)) {
        size_t keyLen = prefs.getBytes(OTA_NVS_KEY, signingKey, sizeof(signingKey));
        signingKeySet = (keyLen == 32);
        prefs.end();
    }
}

void OtaManager::saveSigningKey() {
    Preferences prefs;
    if (prefs.begin(OTA_NVS_NAMESPACE, false)) {
        prefs.putBytes(OTA_NVS_KEY, signingKey, sizeof(signingKey));
        prefs.end();
    }
}
