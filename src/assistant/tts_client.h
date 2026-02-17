/**
 * @file tts_client.h
 * @brief Text-to-speech client using OpenAI TTS API
 *
 * Sends text to OpenAI TTS and streams MP3 audio back for playback.
 */

#ifndef TTS_CLIENT_H
#define TTS_CLIENT_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <functional>

//=============================================================================
// Configuration
//=============================================================================

/** OpenAI TTS API configuration */
#define OPENAI_TTS_HOST "api.openai.com"
#define OPENAI_TTS_PATH "/v1/audio/speech"

/** Audio buffer size for streaming */
#define TTS_AUDIO_BUFFER_SIZE 4096

/** Maximum text length for TTS */
#define TTS_MAX_TEXT_LENGTH 2048

/** HTTP timeout (ms) */
#define TTS_HTTP_TIMEOUT_MS 30000

//=============================================================================
// TTS State and Callbacks
//=============================================================================

/**
 * @enum TTSState
 * @brief Current state of the TTS client
 */
enum class TTSState {
    Idle,           ///< Ready for new request
    Requesting,     ///< Sending request to API
    Streaming,      ///< Receiving and playing audio
    Complete,       ///< Playback complete
    Error           ///< Error occurred
};

/**
 * @brief Callback for audio data chunks
 * @param data Audio data (MP3)
 * @param length Data length
 */
using AudioChunkCallback = std::function<void(const uint8_t* data, size_t length)>;

/**
 * @brief Callback for state changes
 * @param state New state
 */
using TTSStateCallback = std::function<void(TTSState state)>;

/**
 * @brief Callback for errors
 * @param errorMessage Error description
 */
using TTSErrorCallback = std::function<void(const char* errorMessage)>;

//=============================================================================
// Voice Configuration
//=============================================================================

/**
 * @struct VoiceConfig
 * @brief Voice settings for OpenAI TTS
 */
struct VoiceConfig {
    char openAIVoice[32];           ///< Voice name (alloy, echo, fable, onyx, nova, shimmer)
    float speed;                     ///< Speech speed (0.25-4.0)

    VoiceConfig() {
        strcpy(openAIVoice, "nova");
        speed = 0.85f;
    }
};

//=============================================================================
// TTSClient Class
//=============================================================================

/**
 * @class TTSClient
 * @brief Streaming text-to-speech via OpenAI API
 *
 * Sends text to OpenAI TTS API and streams MP3 audio back for playback.
 */
class TTSClient {
public:
    TTSClient();
    ~TTSClient();

    /**
     * @brief Initialize the client
     * @param apiKey OpenAI API key
     * @return true if initialization successful
     */
    bool begin(const char* apiKey);

    /**
     * @brief Cleanup
     */
    void end();

    /**
     * @brief Process streaming (call from loop)
     */
    void loop();

    //-------------------------------------------------------------------------
    // Speech Synthesis
    //-------------------------------------------------------------------------

    /**
     * @brief Start speaking text
     * @param text Text to speak
     * @return true if request started
     */
    bool speak(const char* text);

    /**
     * @brief Stop current playback
     */
    void stop();

    /**
     * @brief Check if currently speaking
     */
    bool isSpeaking() const { return state == TTSState::Streaming; }

    /**
     * @brief Check if idle
     */
    bool isIdle() const { return state == TTSState::Idle; }

    //-------------------------------------------------------------------------
    // Configuration
    //-------------------------------------------------------------------------

    /**
     * @brief Set voice configuration
     */
    void setVoice(const VoiceConfig& config) { voiceConfig = config; }

    /**
     * @brief Get voice configuration
     */
    VoiceConfig& getVoice() { return voiceConfig; }

    /**
     * @brief Set API key
     */
    void setApiKey(const char* key);

    //-------------------------------------------------------------------------
    // State
    //-------------------------------------------------------------------------

    /**
     * @brief Get current state
     */
    TTSState getState() const { return state; }

    /**
     * @brief Get last error message
     */
    const char* getError() const { return lastError; }

    //-------------------------------------------------------------------------
    // Callbacks
    //-------------------------------------------------------------------------

    /**
     * @brief Set audio chunk callback
     * Called with each chunk of audio data received
     */
    void onAudioChunk(AudioChunkCallback callback) { audioChunkCallback = callback; }

    /**
     * @brief Set state change callback
     */
    void onStateChange(TTSStateCallback callback) { stateCallback = callback; }

    /**
     * @brief Set error callback
     */
    void onError(TTSErrorCallback callback) { errorCallback = callback; }

private:
    /**
     * @brief Make OpenAI TTS request (blocking read)
     */
    bool requestOpenAI(const char* text);

    /**
     * @brief Set state and notify callback
     */
    void setState(TTSState newState);

    TTSState state;

    // Configuration
    char apiKey[256];
    VoiceConfig voiceConfig;
    bool initialized;

    // HTTP (no persistent client — created on-demand per request to save ~40KB heap)
    HTTPClient http;
    int contentLength;
    size_t bytesReceived;

    // Audio buffer
    uint8_t audioBuffer[TTS_AUDIO_BUFFER_SIZE];

    // Error handling
    char lastError[128];

    // Callbacks
    AudioChunkCallback audioChunkCallback;
    TTSStateCallback stateCallback;
    TTSErrorCallback errorCallback;
};

#endif // TTS_CLIENT_H
