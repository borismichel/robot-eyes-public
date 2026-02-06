/**
 * @file stt_client.cpp
 * @brief Speech-to-text client using OpenAI Whisper API
 */

#include "stt_client.h"
#include <ArduinoJson.h>

//=============================================================================
// Constructor / Destructor
//=============================================================================

STTClient::STTClient()
    : state(STTState::Idle)
    , initialized(false)
    , audioBuffer(nullptr)
    , audioBufferPos(0)
    , audioBufferSize(STT_MAX_AUDIO_BUFFER)
    , transcriptReady(false)
    , secureClient(nullptr)
    , transcriptCallback(nullptr)
    , errorCallback(nullptr)
{
    memset(apiKey, 0, sizeof(apiKey));
    memset(transcript, 0, sizeof(transcript));
    memset(lastError, 0, sizeof(lastError));
}

STTClient::~STTClient() {
    end();
}

//=============================================================================
// Initialization
//=============================================================================

bool STTClient::begin(const char* key) {
    if (initialized) return true;

    if (!key || strlen(key) == 0) {
        Serial.println("[STT] ERROR: API key required");
        return false;
    }

    strncpy(apiKey, key, sizeof(apiKey) - 1);
    apiKey[sizeof(apiKey) - 1] = '\0';

    // Allocate audio buffer
    audioBuffer = (uint8_t*)malloc(audioBufferSize);
    if (!audioBuffer) {
        Serial.println("[STT] ERROR: Failed to allocate audio buffer");
        return false;
    }

    // Create secure client
    secureClient = new NetworkClientSecure();
    if (!secureClient) {
        Serial.println("[STT] ERROR: Failed to create secure client");
        free(audioBuffer);
        audioBuffer = nullptr;
        return false;
    }

    secureClient->setInsecure();

    initialized = true;
    state = STTState::Idle;
    Serial.printf("[STT] Initialized with OpenAI Whisper (buffer: %d bytes)\n", audioBufferSize);
    return true;
}

void STTClient::end() {
    if (!initialized) return;

    if (audioBuffer) {
        free(audioBuffer);
        audioBuffer = nullptr;
    }

    if (secureClient) {
        delete secureClient;
        secureClient = nullptr;
    }

    initialized = false;
    state = STTState::Idle;
    Serial.println("[STT] Shutdown");
}

//=============================================================================
// Recording Control
//=============================================================================

bool STTClient::startRecording() {
    if (!initialized) {
        Serial.println("[STT] Not initialized");
        return false;
    }

    if (state == STTState::Recording) {
        return true;  // Already recording
    }

    // Clear buffer
    audioBufferPos = 0;
    transcriptReady = false;
    memset(transcript, 0, sizeof(transcript));

    state = STTState::Recording;
    Serial.println("[STT] Recording started");
    return true;
}

bool STTClient::stopRecording() {
    if (state != STTState::Recording) {
        return false;
    }

    Serial.printf("[STT] Recording stopped (%d bytes)\n", audioBufferPos);

    if (audioBufferPos < 1000) {
        // Too short to transcribe
        snprintf(lastError, sizeof(lastError), "Recording too short");
        state = STTState::Idle;
        if (errorCallback) {
            errorCallback(lastError);
        }
        return false;
    }

    // Transcribe the audio
    return transcribe();
}

bool STTClient::sendAudio(const uint8_t* data, size_t length) {
    if (state != STTState::Recording) {
        return false;
    }

    // Check buffer space
    if (audioBufferPos + length > audioBufferSize) {
        // Buffer full - could expand or just stop
        Serial.println("[STT] Buffer full");
        return false;
    }

    // Copy audio data to buffer
    memcpy(audioBuffer + audioBufferPos, data, length);
    audioBufferPos += length;

    return true;
}

void STTClient::clearTranscript() {
    memset(transcript, 0, sizeof(transcript));
    transcriptReady = false;
}

//=============================================================================
// Transcription
//=============================================================================

bool STTClient::transcribe() {
    state = STTState::Transcribing;
    Serial.println("[STT] Sending to Whisper API...");

    // Build URL
    String url = "https://";
    url += WHISPER_API_HOST;
    url += WHISPER_API_PATH;

    // Generate boundary for multipart form
    String boundary = "----ESP32Boundary" + String(millis());

    // Build WAV file (header + audio data)
    uint32_t wavDataSize = audioBufferPos;

    uint8_t wavHeader[44];
    buildWavHeader(wavHeader, wavDataSize);

    // Build multipart form body
    String formStart = "--" + boundary + "\r\n";
    formStart += "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n";
    formStart += "Content-Type: audio/wav\r\n\r\n";

    String formModel = "\r\n--" + boundary + "\r\n";
    formModel += "Content-Disposition: form-data; name=\"model\"\r\n\r\n";
    formModel += WHISPER_MODEL;

    String formEnd = "\r\n--" + boundary + "--\r\n";

    // Calculate total content length
    size_t contentLength = formStart.length() + 44 + wavDataSize + formModel.length() + formEnd.length();

    // Start HTTP request
    http.begin(*secureClient, url);
    http.setTimeout(STT_HTTP_TIMEOUT_MS);
    http.addHeader("Authorization", String("Bearer ") + apiKey);
    http.addHeader("Content-Type", "multipart/form-data; boundary=" + boundary);
    http.addHeader("Content-Length", String(contentLength));

    // Allocate buffer for complete request body
    uint8_t* requestBody = (uint8_t*)malloc(contentLength);
    if (!requestBody) {
        Serial.println("[STT] Failed to allocate request buffer");
        snprintf(lastError, sizeof(lastError), "Memory allocation failed");
        state = STTState::Error;
        if (errorCallback) {
            errorCallback(lastError);
        }
        return false;
    }

    size_t offset = 0;

    // Copy form start
    memcpy(requestBody + offset, formStart.c_str(), formStart.length());
    offset += formStart.length();

    // Copy WAV header
    memcpy(requestBody + offset, wavHeader, 44);
    offset += 44;

    // Copy audio data
    memcpy(requestBody + offset, audioBuffer, wavDataSize);
    offset += wavDataSize;

    // Copy model field
    memcpy(requestBody + offset, formModel.c_str(), formModel.length());
    offset += formModel.length();

    // Copy form end
    memcpy(requestBody + offset, formEnd.c_str(), formEnd.length());
    offset += formEnd.length();

    // Send POST request
    int httpCode = http.POST(requestBody, contentLength);

    free(requestBody);

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[STT] HTTP error: %d\n", httpCode);
        String errBody = http.getString();
        Serial.printf("[STT] Response: %.200s\n", errBody.c_str());

        snprintf(lastError, sizeof(lastError), "HTTP %d", httpCode);
        state = STTState::Error;
        http.end();

        if (errorCallback) {
            errorCallback(lastError);
        }
        return false;
    }

    // Parse response
    String response = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);

    if (error) {
        Serial.printf("[STT] JSON parse error: %s\n", error.c_str());
        snprintf(lastError, sizeof(lastError), "JSON parse error");
        state = STTState::Error;

        if (errorCallback) {
            errorCallback(lastError);
        }
        return false;
    }

    // Check for error in response
    if (doc["error"].is<JsonObject>()) {
        const char* errMsg = doc["error"]["message"];
        snprintf(lastError, sizeof(lastError), "%s", errMsg ? errMsg : "API error");
        state = STTState::Error;

        if (errorCallback) {
            errorCallback(lastError);
        }
        return false;
    }

    // Get transcript
    const char* text = doc["text"];
    if (text) {
        strncpy(transcript, text, sizeof(transcript) - 1);
        transcript[sizeof(transcript) - 1] = '\0';
        transcriptReady = true;

        Serial.printf("[STT] Transcript: %s\n", transcript);

        if (transcriptCallback) {
            transcriptCallback(transcript, true);
        }
    }

    state = STTState::Idle;
    return true;
}

//=============================================================================
// WAV Header Building
//=============================================================================

void STTClient::buildWavHeader(uint8_t* header, uint32_t dataSize) {
    // WAV file format for 16-bit PCM, 16kHz, mono
    uint32_t sampleRate = 16000;
    uint16_t numChannels = 1;
    uint16_t bitsPerSample = 16;
    uint32_t byteRate = sampleRate * numChannels * bitsPerSample / 8;
    uint16_t blockAlign = numChannels * bitsPerSample / 8;
    uint32_t chunkSize = 36 + dataSize;

    // RIFF header
    header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
    header[4] = chunkSize & 0xFF;
    header[5] = (chunkSize >> 8) & 0xFF;
    header[6] = (chunkSize >> 16) & 0xFF;
    header[7] = (chunkSize >> 24) & 0xFF;
    header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';

    // fmt subchunk
    header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
    header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;  // Subchunk1Size (16 for PCM)
    header[20] = 1; header[21] = 0;  // AudioFormat (1 = PCM)
    header[22] = numChannels & 0xFF; header[23] = (numChannels >> 8) & 0xFF;
    header[24] = sampleRate & 0xFF;
    header[25] = (sampleRate >> 8) & 0xFF;
    header[26] = (sampleRate >> 16) & 0xFF;
    header[27] = (sampleRate >> 24) & 0xFF;
    header[28] = byteRate & 0xFF;
    header[29] = (byteRate >> 8) & 0xFF;
    header[30] = (byteRate >> 16) & 0xFF;
    header[31] = (byteRate >> 24) & 0xFF;
    header[32] = blockAlign & 0xFF; header[33] = (blockAlign >> 8) & 0xFF;
    header[34] = bitsPerSample & 0xFF; header[35] = (bitsPerSample >> 8) & 0xFF;

    // data subchunk
    header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
    header[40] = dataSize & 0xFF;
    header[41] = (dataSize >> 8) & 0xFF;
    header[42] = (dataSize >> 16) & 0xFF;
    header[43] = (dataSize >> 24) & 0xFF;
}
