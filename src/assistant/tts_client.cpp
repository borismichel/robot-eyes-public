/**
 * @file tts_client.cpp
 * @brief Text-to-speech client implementation (OpenAI only)
 */

#include "tts_client.h"
#include "http_helpers.h"
#include <ArduinoJson.h>
#include <NetworkClientSecure.h>

//=============================================================================
// Constructor / Destructor
//=============================================================================

TTSClient::TTSClient()
    : state(TTSState::Idle)
    , initialized(false)
    , secureClient(nullptr)
    , contentLength(0)
    , bytesReceived(0)
    , audioChunkCallback(nullptr)
    , stateCallback(nullptr)
    , errorCallback(nullptr)
{
    memset(apiKey, 0, sizeof(apiKey));
    memset(audioBuffer, 0, sizeof(audioBuffer));
    memset(lastError, 0, sizeof(lastError));
}

TTSClient::~TTSClient() {
    end();
}

//=============================================================================
// Initialization
//=============================================================================

bool TTSClient::begin(const char* key) {
    if (initialized) return true;

    if (!key || strlen(key) == 0) {
        Serial.println("[TTS] ERROR: API key required");
        return false;
    }

    setApiKey(key);

    secureClient = HttpHelpers::createSecureClient();
    if (!secureClient) {
        Serial.println("[TTS] ERROR: Failed to create secure client");
        return false;
    }

    initialized = true;
    state = TTSState::Idle;

    Serial.println("[TTS] Initialized with OpenAI provider");
    return true;
}

void TTSClient::end() {
    if (!initialized) return;

    stop();

    if (secureClient) {
        delete secureClient;
        secureClient = nullptr;
    }

    initialized = false;
    Serial.println("[TTS] Shutdown");
}

void TTSClient::setApiKey(const char* key) {
    if (key) {
        strncpy(apiKey, key, sizeof(apiKey) - 1);
        apiKey[sizeof(apiKey) - 1] = '\0';
    }
}

//=============================================================================
// Speech Synthesis
//=============================================================================

bool TTSClient::speak(const char* text) {
    if (!initialized) {
        Serial.println("[TTS] ERROR: Not initialized");
        return false;
    }

    if (state == TTSState::Streaming || state == TTSState::Requesting) {
        Serial.println("[TTS] Already speaking");
        return false;
    }

    if (!text || strlen(text) == 0) {
        Serial.println("[TTS] ERROR: Empty text");
        return false;
    }

    if (strlen(text) > TTS_MAX_TEXT_LENGTH) {
        Serial.println("[TTS] ERROR: Text too long");
        return false;
    }

    Serial.printf("[TTS] Speaking: %.50s%s\n", text, strlen(text) > 50 ? "..." : "");

    return requestOpenAI(text);
}

void TTSClient::stop() {
    if (state == TTSState::Idle) return;

    Serial.println("[TTS] Stopping playback");
    http.end();
    setState(TTSState::Idle);
}

//=============================================================================
// Main Loop
//=============================================================================

void TTSClient::loop() {
    // OpenAI TTS uses blocking inline read — nothing to do here
}

//=============================================================================
// OpenAI Implementation
//=============================================================================

bool TTSClient::requestOpenAI(const char* text) {
    setState(TTSState::Requesting);

    // Build URL
    String url = "https://";
    url += OPENAI_TTS_HOST;
    url += OPENAI_TTS_PATH;

    // Build request body
    JsonDocument doc;
    doc["model"] = "tts-1";
    doc["input"] = text;
    doc["voice"] = voiceConfig.openAIVoice;
    doc["speed"] = voiceConfig.speed;
    doc["response_format"] = "mp3";

    String body;
    serializeJson(doc, body);

    // Reset secure client to clear any stale connection state
    HttpHelpers::resetSecureClient(secureClient);
    if (!secureClient) {
        Serial.println("[TTS] Failed to create secure client");
        setState(TTSState::Error);
        return false;
    }

    // Make request
    http.begin(*secureClient, url);
    http.setTimeout(TTS_HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + apiKey);

    uint32_t ttsHttpStart = millis();
    int httpCode = http.POST(body);
    uint32_t ttsHttpElapsed = millis() - ttsHttpStart;

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[TTS] OpenAI error: %d after %lums\n", httpCode, ttsHttpElapsed);
        String response = http.getString();
        Serial.printf("[TTS] Response: %s\n", response.c_str());

        snprintf(lastError, sizeof(lastError), "HTTP %d", httpCode);
        http.end();
        setState(TTSState::Error);

        if (errorCallback) {
            errorCallback(lastError);
        }
        return false;
    }

    // Read entire response body inline (blocking).
    // The whole voice pipeline is synchronous (STT blocks, LLM blocks),
    // so blocking here is fine and avoids async stream detection issues.
    contentLength = http.getSize();
    bytesReceived = 0;

    Serial.printf("[TTS] HTTP 200 in %lums, reading %d bytes of audio...\n", ttsHttpElapsed, contentLength);
    setState(TTSState::Streaming);

    NetworkClient* stream = http.getStreamPtr();
    if (stream) {
        uint32_t readStart = millis();
        while (stream->connected() || stream->available()) {
            size_t available = stream->available();
            if (available > 0) {
                size_t toRead = min(available, sizeof(audioBuffer));
                size_t bytesRead = stream->readBytes(audioBuffer, toRead);
                if (bytesRead > 0) {
                    bytesReceived += bytesRead;
                    if (audioChunkCallback) {
                        audioChunkCallback(audioBuffer, bytesRead);
                    }
                }
            } else {
                delay(1);  // Yield while waiting for more data
            }
            // Safety timeout
            if (millis() - readStart > TTS_HTTP_TIMEOUT_MS) {
                Serial.println("[TTS] Read timeout");
                break;
            }
        }
    }

    http.end();

    Serial.printf("[TTS] Read %u bytes of audio\n", bytesReceived);
    setState(TTSState::Complete);

    return true;
}

//=============================================================================
// State Management
//=============================================================================

void TTSClient::setState(TTSState newState) {
    if (state != newState) {
        state = newState;
        if (stateCallback) {
            stateCallback(state);
        }
    }
}
