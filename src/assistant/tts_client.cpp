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

    initialized = true;
    state = TTSState::Idle;

    Serial.println("[TTS] Initialized with OpenAI provider");
    return true;
}

void TTSClient::end() {
    if (!initialized) return;

    stop();

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

    Serial.printf("[TTS] Speaking (voice=%s speed=%.2f): %.50s%s\n",
                  voiceConfig.openAIVoice, voiceConfig.speed,
                  text, strlen(text) > 50 ? "..." : "");

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
    doc["response_format"] = "wav";

    String body;
    serializeJson(doc, body);

    // Create SSL client on-demand (saves ~40KB heap when idle)
    NetworkClientSecure* secureClient = HttpHelpers::createSecureClient();
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
        delete secureClient;
        setState(TTSState::Error);

        if (errorCallback) {
            errorCallback(lastError);
        }
        return false;
    }

    // Read entire response body using writeToStream which handles
    // chunked transfer encoding transparently. The raw getStreamPtr()
    // was leaking chunk headers (e.g. "765\r\n") into the audio data.
    contentLength = http.getSize();
    bytesReceived = 0;

    Serial.printf("[TTS] HTTP 200 in %lums, reading %d bytes of audio...\n", ttsHttpElapsed, contentLength);
    setState(TTSState::Streaming);

    // Stream adapter that forwards dechunked data to our callback.
    // writeToStream requires Stream* — we only use the write side.
    class ChunkForwarder : public Stream {
    public:
        ChunkForwarder(AudioChunkCallback& cb, size_t& received)
            : callback(cb), bytesReceived(received) {}
        size_t write(uint8_t b) override { return write(&b, 1); }
        size_t write(const uint8_t* buf, size_t size) override {
            bytesReceived += size;
            if (callback) callback(buf, size);
            return size;
        }
        int available() override { return 0; }
        int read() override { return -1; }
        int peek() override { return -1; }
    private:
        AudioChunkCallback& callback;
        size_t& bytesReceived;
    };

    ChunkForwarder forwarder(audioChunkCallback, bytesReceived);
    http.writeToStream(&forwarder);

    http.end();
    delete secureClient;

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
