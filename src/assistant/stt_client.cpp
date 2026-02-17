/**
 * @file stt_client.cpp
 * @brief Speech-to-text client using OpenAI Whisper API
 */

#include "stt_client.h"
#include "http_helpers.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

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
    , transcriptCallback(nullptr)
    , errorCallback(nullptr)
{
    memset(apiKey, 0, sizeof(apiKey));
    memset(language, 0, sizeof(language));
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

    // Allocate audio buffer (use PSRAM for large buffer)
    audioBuffer = (uint8_t*)ps_malloc(audioBufferSize);
    if (!audioBuffer) {
        Serial.println("[STT] ERROR: Failed to allocate audio buffer");
        return false;
    }

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

void STTClient::logAudioStats() {
    Serial.printf("[STT] Buffer: %d bytes\n", audioBufferPos);
    if (audioBufferPos >= 2) {
        const int16_t* samples = (const int16_t*)audioBuffer;
        size_t numSamples = audioBufferPos / 2;
        int16_t peak = 0;
        int64_t sumAbs = 0;
        size_t zeroCount = 0;
        for (size_t i = 0; i < numSamples; i++) {
            int16_t s = samples[i] < 0 ? -samples[i] : samples[i];
            if (s > peak) peak = s;
            sumAbs += s;
            if (samples[i] == 0) zeroCount++;
        }
        float avgAbs = (float)sumAbs / numSamples;
        float cf = avgAbs > 0 ? (float)peak / avgAbs : 0;
        Serial.printf("[STT] Audio stats: peak=%d avg=%.1f CF=%.1f zeros=%d/%d (%.0f%%)\n",
                      peak, avgAbs, cf, zeroCount, numSamples,
                      100.0f * zeroCount / numSamples);
        if (cf < 3.5f && peak > 500) {
            Serial.println("[STT] Warning: low crest factor suggests noise, not speech");
        }
    }
}

void STTClient::cancelRecording() {
    audioBufferPos = 0;
    state = STTState::Idle;
    Serial.println("[STT] Recording cancelled, buffer cleared");
}

bool STTClient::stopRecording() {
    if (state != STTState::Recording) {
        return false;
    }

    Serial.printf("[STT] Recording stopped (%d bytes)\n", audioBufferPos);
    logAudioStats();

    if (audioBufferPos < 1000) {
        // Too short to transcribe
        snprintf(lastError, sizeof(lastError), "Recording too short");
        state = STTState::Idle;
        if (errorCallback) {
            errorCallback(lastError);
        }
        return false;
    }

    // Check audio quality before sending to Whisper.
    // Two checks: (1) minimum peak for silence, (2) crest factor for noise.
    {
        const int16_t* samples = (const int16_t*)audioBuffer;
        size_t numSamples = audioBufferPos / 2;
        int16_t peak = 0;
        int64_t sumAbs = 0;
        for (size_t i = 0; i < numSamples; i++) {
            int16_t s = samples[i] < 0 ? -samples[i] : samples[i];
            if (s > peak) peak = s;
            sumAbs += s;
        }

        if (peak < 500) {
            Serial.printf("[STT] Audio too quiet (peak=%d), skipping Whisper\n", peak);
            snprintf(lastError, sizeof(lastError), "No speech detected");
            state = STTState::Idle;
            return false;
        }

        // Crest factor = peak / avgAbs. Speech typically has CF > 4 (dynamic range
        // between peaks and average). Amplified noise has CF ≈ 2-3.5 (flat, dense).
        // When mic gain is too high, noise fills the buffer and Whisper hallucinates
        // in random languages. Reject recordings that look like noise.
        float avgAbs = (float)sumAbs / numSamples;
        if (avgAbs > 0) {
            float crestFactor = (float)peak / avgAbs;
            if (crestFactor < 3.5f) {
                Serial.printf("[STT] Audio looks like noise, not speech (CF=%.1f, peak=%d, avg=%.0f)\n",
                              crestFactor, peak, avgAbs);
                Serial.println("[STT] Hint: try lowering Mic Gain in settings");
                snprintf(lastError, sizeof(lastError), "No speech detected (noise)");
                state = STTState::Idle;
                return false;
            }
        }
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

bool STTClient::saveAsWav(const char* path) {
    if (audioBufferPos < 100) return false;

    File f = LittleFS.open(path, "w");
    if (!f) {
        Serial.println("[STT] Failed to open WAV file for writing");
        return false;
    }

    // Find peak to normalize audio for audible playback
    const int16_t* samples = (const int16_t*)audioBuffer;
    size_t numSamples = audioBufferPos / 2;
    int16_t peak = 0;
    for (size_t i = 0; i < numSamples; i++) {
        int16_t s = samples[i] < 0 ? -samples[i] : samples[i];
        if (s > peak) peak = s;
    }

    // Scale to 80% of full range for comfortable playback
    float scale = (peak > 100) ? (26000.0f / peak) : 1.0f;
    Serial.printf("[STT] WAV normalize: peak=%d scale=%.2f\n", peak, scale);

    uint8_t header[44];
    buildWavHeader(header, audioBufferPos);
    f.write(header, 44);

    // Write normalized samples in chunks
    const size_t CHUNK = 512;
    int16_t chunk[CHUNK];
    for (size_t i = 0; i < numSamples; i += CHUNK) {
        size_t count = (i + CHUNK <= numSamples) ? CHUNK : (numSamples - i);
        for (size_t j = 0; j < count; j++) {
            int32_t s = (int32_t)(samples[i + j] * scale);
            chunk[j] = (int16_t)constrain(s, -32767, 32767);
        }
        f.write((uint8_t*)chunk, count * 2);
    }

    f.close();
    Serial.printf("[STT] Saved %d bytes as WAV to %s\n", audioBufferPos + 44, path);
    return true;
}

//=============================================================================
// Transcription
//=============================================================================

bool STTClient::transcribe() {
    state = STTState::Transcribing;
    uint32_t whisperStart = millis();
    Serial.printf("[STT] Sending %d bytes to Whisper API (lang=%s)...\n",
                  audioBufferPos, language[0] ? language : "auto");
    if (language[0] == '\0') {
        Serial.println("[STT] Tip: set STT Language in web settings to prevent hallucinations");
    }

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

    // Optional language hint (ISO 639-1)
    String formLanguage;
    if (language[0] != '\0') {
        formLanguage = "\r\n--" + boundary + "\r\n";
        formLanguage += "Content-Disposition: form-data; name=\"language\"\r\n\r\n";
        formLanguage += language;
    }

    String formEnd = "\r\n--" + boundary + "--\r\n";

    // Calculate total content length
    size_t contentLength = formStart.length() + 44 + wavDataSize + formModel.length() + formLanguage.length() + formEnd.length();

    // Create SSL client on-demand (saves ~40KB heap when idle)
    NetworkClientSecure* secureClient = HttpHelpers::createSecureClient();
    if (!secureClient) {
        Serial.println("[STT] Failed to create secure client");
        state = STTState::Error;
        if (errorCallback) errorCallback("Client alloc failed");
        return false;
    }

    // Connect and send headers manually, then stream the body in chunks.
    // This avoids malloc'ing 140KB+ and prevents TLS write failures from
    // trying to push the entire payload through SSL at once.
    if (!secureClient->connect(WHISPER_API_HOST, 443)) {
        Serial.println("[STT] Failed to connect to Whisper API");
        delete secureClient;
        snprintf(lastError, sizeof(lastError), "Connection failed");
        state = STTState::Error;
        if (errorCallback) errorCallback(lastError);
        return false;
    }

    // Send HTTP headers
    secureClient->printf("POST %s HTTP/1.1\r\n", WHISPER_API_PATH);
    secureClient->printf("Host: %s\r\n", WHISPER_API_HOST);
    secureClient->printf("Authorization: Bearer %s\r\n", apiKey);
    secureClient->printf("Content-Type: multipart/form-data; boundary=%s\r\n", boundary.c_str());
    secureClient->printf("Content-Length: %u\r\n", contentLength);
    secureClient->print("Connection: close\r\n\r\n");

    // Stream multipart form body in pieces
    secureClient->print(formStart);
    secureClient->write(wavHeader, 44);

    // Stream audio data in 4KB chunks
    const size_t CHUNK_SIZE = 4096;
    size_t sent = 0;
    while (sent < wavDataSize) {
        size_t toSend = min(CHUNK_SIZE, (size_t)(wavDataSize - sent));
        size_t written = secureClient->write(audioBuffer + sent, toSend);
        if (written == 0) {
            Serial.printf("[STT] Write failed at %u/%u bytes\n", sent, wavDataSize);
            secureClient->stop();
            delete secureClient;
            snprintf(lastError, sizeof(lastError), "Write failed");
            state = STTState::Error;
            if (errorCallback) errorCallback(lastError);
            return false;
        }
        sent += written;
    }

    secureClient->print(formModel);
    if (formLanguage.length() > 0) {
        secureClient->print(formLanguage);
    }
    secureClient->print(formEnd);

    // Read HTTP response status line
    uint32_t responseStart = millis();
    while (!secureClient->available()) {
        if (millis() - responseStart > STT_HTTP_TIMEOUT_MS) {
            Serial.println("[STT] Response timeout");
            secureClient->stop();
            delete secureClient;
            snprintf(lastError, sizeof(lastError), "Response timeout");
            state = STTState::Error;
            if (errorCallback) errorCallback(lastError);
            return false;
        }
        delay(10);
    }

    // Parse status line
    String statusLine = secureClient->readStringUntil('\n');
    int httpCode = 0;
    if (statusLine.startsWith("HTTP/1.")) {
        httpCode = statusLine.substring(9, 12).toInt();
    }

    // Skip headers (check for chunked encoding)
    bool isChunked = false;
    while (secureClient->connected()) {
        String line = secureClient->readStringUntil('\n');
        if (line == "\r" || line.length() == 0) break;
        if (line.indexOf("chunked") >= 0) {
            isChunked = true;
            Serial.println("[STT] Warning: response is chunked");
        }
    }

    // Read response body
    String responseBody;
    while (secureClient->connected() || secureClient->available()) {
        if (secureClient->available()) {
            responseBody += (char)secureClient->read();
        } else {
            delay(1);
        }
        if (millis() - responseStart > STT_HTTP_TIMEOUT_MS) break;
    }
    secureClient->stop();
    delete secureClient;

    Serial.printf("[STT] HTTP %d, body (%d bytes): %.200s\n", httpCode, responseBody.length(), responseBody.c_str());

    if (httpCode != 200) {
        snprintf(lastError, sizeof(lastError), "HTTP %d", httpCode);
        state = STTState::Error;

        if (errorCallback) {
            errorCallback(lastError);
        }
        return false;
    }

    // If response is chunked, strip chunk headers before parsing
    if (isChunked && responseBody.length() > 0) {
        int jsonStart = responseBody.indexOf('{');
        int jsonEnd = responseBody.lastIndexOf('}');
        if (jsonStart >= 0 && jsonEnd > jsonStart) {
            responseBody = responseBody.substring(jsonStart, jsonEnd + 1);
            Serial.printf("[STT] Dechunked body: %s\n", responseBody.c_str());
        }
    }

    // Parse response
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, responseBody);

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

        Serial.printf("[STT] Whisper responded in %lums: %s\n", millis() - whisperStart, transcript);

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
