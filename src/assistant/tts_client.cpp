/**
 * @file tts_client.cpp
 * @brief Text-to-speech client implementation
 */

#include "tts_client.h"
#include <ArduinoJson.h>
#include <NetworkClientSecure.h>

//=============================================================================
// Constructor / Destructor
//=============================================================================

TTSClient::TTSClient()
    : provider(TTSProvider::ElevenLabs)
    , state(TTSState::Idle)
    , initialized(false)
    , secureClient(nullptr)
    , streamActive(false)
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

bool TTSClient::begin(TTSProvider prov, const char* key) {
    if (initialized) return true;

    if (!key || strlen(key) == 0) {
        Serial.println("[TTS] ERROR: API key required");
        return false;
    }

    provider = prov;
    setApiKey(key);

    // Create secure client
    secureClient = new NetworkClientSecure();
    if (!secureClient) {
        Serial.println("[TTS] ERROR: Failed to create secure client");
        return false;
    }

    // Skip certificate verification (for simplicity)
    // In production, you'd want to verify certificates
    secureClient->setInsecure();

    initialized = true;
    state = TTSState::Idle;

    Serial.printf("[TTS] Initialized with %s provider\n",
                  provider == TTSProvider::ElevenLabs ? "ElevenLabs" : "OpenAI");
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

    bool success = false;
    if (provider == TTSProvider::ElevenLabs) {
        success = requestElevenLabs(text);
    } else {
        success = requestOpenAI(text);
    }

    return success;
}

void TTSClient::stop() {
    if (state == TTSState::Idle) return;

    Serial.println("[TTS] Stopping playback");

    if (streamActive) {
        http.end();
        streamActive = false;
    }

    setState(TTSState::Idle);
}

//=============================================================================
// Main Loop
//=============================================================================

void TTSClient::loop() {
    if (!initialized) return;

    if (state == TTSState::Streaming && streamActive) {
        processStream();
    }
}

//=============================================================================
// ElevenLabs Implementation
//=============================================================================

bool TTSClient::requestElevenLabs(const char* text) {
    setState(TTSState::Requesting);

    // Build URL
    String url = "https://";
    url += ELEVENLABS_API_HOST;
    url += ELEVENLABS_API_PATH;
    url += "/";
    url += voiceConfig.elevenLabsVoiceId;
    url += "/stream";

    // Build request body
    JsonDocument doc;
    doc["text"] = text;
    doc["model_id"] = "eleven_turbo_v2";

    JsonObject voiceSettings = doc["voice_settings"].to<JsonObject>();
    voiceSettings["stability"] = voiceConfig.stability;
    voiceSettings["similarity_boost"] = voiceConfig.similarityBoost;

    String body;
    serializeJson(doc, body);

    // Make request
    http.begin(*secureClient, url);
    http.setTimeout(TTS_HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("xi-api-key", apiKey);
    http.addHeader("Accept", "audio/mpeg");

    int httpCode = http.POST(body);

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[TTS] ElevenLabs error: %d\n", httpCode);
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

    // Get content length (may be -1 for chunked)
    contentLength = http.getSize();
    bytesReceived = 0;
    streamActive = true;

    Serial.printf("[TTS] Streaming audio (%d bytes)\n", contentLength);
    setState(TTSState::Streaming);

    return true;
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

    // Make request
    http.begin(*secureClient, url);
    http.setTimeout(TTS_HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", String("Bearer ") + apiKey);

    int httpCode = http.POST(body);

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[TTS] OpenAI error: %d\n", httpCode);
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

    // Get content length
    contentLength = http.getSize();
    bytesReceived = 0;
    streamActive = true;

    Serial.printf("[TTS] Streaming audio (%d bytes)\n", contentLength);
    setState(TTSState::Streaming);

    return true;
}

//=============================================================================
// Stream Processing
//=============================================================================

void TTSClient::processStream() {
    NetworkClient* stream = http.getStreamPtr();
    if (!stream) {
        Serial.println("[TTS] Stream lost");
        setState(TTSState::Error);
        streamActive = false;
        return;
    }

    // Check if data available
    size_t available = stream->available();
    if (available == 0) {
        // Check if transfer complete
        if (contentLength > 0 && bytesReceived >= contentLength) {
            Serial.printf("[TTS] Stream complete (%u bytes)\n", bytesReceived);
            http.end();
            streamActive = false;
            setState(TTSState::Complete);
        } else if (!stream->connected()) {
            // Connection closed
            if (bytesReceived > 0) {
                Serial.printf("[TTS] Stream ended (%u bytes)\n", bytesReceived);
                setState(TTSState::Complete);
            } else {
                Serial.println("[TTS] Stream disconnected");
                setState(TTSState::Error);
            }
            http.end();
            streamActive = false;
        }
        return;
    }

    // Read chunk
    size_t toRead = min(available, sizeof(audioBuffer));
    size_t bytesRead = stream->readBytes(audioBuffer, toRead);

    if (bytesRead > 0) {
        bytesReceived += bytesRead;

        // Forward to callback for playback
        if (audioChunkCallback) {
            audioChunkCallback(audioBuffer, bytesRead);
        }
    }
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
