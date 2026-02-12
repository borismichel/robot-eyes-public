/**
 * @file ota_manager.h
 * @brief OTA firmware update manager with safety features
 *
 * Handles firmware upload, validation, and installation with:
 * - Chunked HTTP upload support
 * - Pre-flash image validation
 * - Progress tracking
 * - Automatic rollback on boot failure
 * - Optional HMAC-SHA256 signature verification
 */

#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/md.h>

// Signature size (HMAC-SHA256 = 32 bytes)
#define OTA_SIGNATURE_SIZE 32

// OTA operation states
enum class OtaState {
    Idle,
    Uploading,
    Verifying,
    Installing,
    Success,
    Error
};

/**
 * @class OtaManager
 * @brief Manages OTA firmware updates with safety features
 */
class OtaManager {
public:
    OtaManager();

    /**
     * @brief Initialize OTA manager and validate current boot
     * Call in setup() - handles rollback state validation
     */
    void begin();

    /**
     * @brief Start a new OTA upload session
     * @param totalSize Expected firmware size in bytes
     * @return true if ready to receive chunks
     */
    bool startUpload(size_t totalSize);

    /**
     * @brief Write a chunk of firmware data
     * @param data Pointer to chunk data
     * @param length Length of chunk
     * @return true if chunk written successfully
     */
    bool writeChunk(const uint8_t* data, size_t length);

    /**
     * @brief Finalize and verify the firmware
     * @return true if firmware valid and ready to install
     */
    bool finishUpload();

    /**
     * @brief Abort current upload
     */
    void cancelUpload();

    /**
     * @brief Get current OTA state
     */
    OtaState getState() const { return state; }

    /**
     * @brief Get human-readable state string
     */
    const char* getStateString() const;

    /**
     * @brief Get upload progress (0-100)
     */
    int getProgress() const;

    /**
     * @brief Get bytes received so far
     */
    size_t getBytesReceived() const { return bytesReceived; }

    /**
     * @brief Get total expected bytes
     */
    size_t getTotalBytes() const { return totalBytes; }

    /**
     * @brief Get error message (if state is Error)
     */
    const char* getErrorMessage() const { return errorMessage; }

    /**
     * @brief Get current firmware version
     */
    static const char* getVersion();

    /**
     * @brief Get build date/time
     */
    static const char* getBuildDate();

    /**
     * @brief Get running partition label
     */
    const char* getPartitionLabel() const;

    /**
     * @brief Get OTA partition size
     */
    size_t getOtaPartitionSize() const;

    /**
     * @brief Check if rollback is possible
     */
    bool canRollback() const;

    /**
     * @brief Force rollback to previous firmware
     * @return true if rollback initiated (device will restart)
     */
    bool rollback();

    /**
     * @brief Restart the device
     */
    void restart();

    /**
     * @brief Set signing key for firmware verification
     * @param key 32-byte signing key (or nullptr to disable)
     * @param keyLen Length of key (must be 32)
     * @return true if key set successfully
     */
    bool setSigningKey(const uint8_t* key, size_t keyLen);

    /**
     * @brief Check if signing key is configured
     */
    bool hasSigningKey() const { return signingKeySet; }

    /**
     * @brief Clear signing key (disable signature verification)
     */
    void clearSigningKey();

private:
    OtaState state;
    esp_ota_handle_t otaHandle;
    const esp_partition_t* updatePartition;
    const esp_partition_t* runningPartition;
    size_t bytesReceived;
    size_t totalBytes;
    size_t firmwareSize;  // Total size minus signature
    char errorMessage[64];
    bool headerValidated;

    // Signature verification
    bool signingKeySet;
    uint8_t signingKey[32];
    mbedtls_md_context_t hmacCtx;
    uint8_t receivedSignature[OTA_SIGNATURE_SIZE];
    uint8_t signatureBuffer[OTA_SIGNATURE_SIZE];  // Rolling buffer for last 32 bytes
    size_t signatureBufferPos;

    bool validateImageHeader(const uint8_t* data, size_t length);
    void setError(const char* msg);
    void reset();
    bool verifySignature();
    void loadSigningKey();
    void saveSigningKey();
};

#endif // OTA_MANAGER_H
