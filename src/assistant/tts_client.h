/**
 * @file tts_client.h
 * @brief Text-to-speech client supporting multiple providers
 *
 * Streams audio from TTS providers and plays back via I2S.
 * Supports ElevenLabs and OpenAI TTS APIs.
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

/** TTS provider selection */
enum class TTSProvider {
    ElevenLabs,
    OpenAI
};

/** ElevenLabs API configuration */
#define ELEVENLABS_API_HOST "api.elevenlabs.io"
#define ELEVENLABS_API_PATH "/v1/text-to-speech"

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
 * @brief Voice settings for TTS
 */
struct VoiceConfig {
    // ElevenLabs settings
    char elevenLabsVoiceId[64];     ///< Voice ID (e.g., "21m00Tcm4TlvDq8ikWAM")
    float stability;                 ///< Voice stability (0.0-1.0)
    float similarityBoost;           ///< Similarity boost (0.0-1.0)

    // OpenAI settings
    char openAIVoice[32];           ///< Voice name (alloy, echo, fable, onyx, nova, shimmer)
    float speed;                     ///< Speech speed (0.25-4.0)

    VoiceConfig() {
        // ElevenLabs defaults
        strcpy(elevenLabsVoiceId, "21m00Tcm4TlvDq8ikWAM");  // Rachel
        stability = 0.5f;
        similarityBoost = 0.75f;

        // OpenAI defaults
        strcpy(openAIVoice, "nova");
        speed = 1.0f;
    }
};

//=============================================================================
// TTSClient Class
//=============================================================================

/**
 * @class TTSClient
 * @brief Streaming text-to-speech via cloud APIs
 *
 * Sends text to TTS API and streams audio back for playback.
 * Supports ElevenLabs and OpenAI providers.
 */
class TTSClient {
public:
    TTSClient();
    ~TTSClient();

    /**
     * @brief Initialize the client
     * @param provider TTS provider to use
     * @param apiKey API key for the provider
     * @return true if initialization successful
     */
    bool begin(TTSProvider provider, const char* apiKey);

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

    /**
     * @brief Change provider
     */
    void setProvider(TTSProvider p) { provider = p; }

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
     * @brief Make ElevenLabs TTS request
     */
    bool requestElevenLabs(const char* text);

    /**
     * @brief Make OpenAI TTS request
     */
    bool requestOpenAI(const char* text);

    /**
     * @brief Process streaming response
     */
    void processStream();

    /**
     * @brief Set state and notify callback
     */
    void setState(TTSState newState);

    TTSProvider provider;
    TTSState state;

    // Configuration
    char apiKey[128];
    VoiceConfig voiceConfig;
    bool initialized;

    // HTTP client for streaming
    HTTPClient http;
    NetworkClientSecure* secureClient;
    bool streamActive;
    size_t contentLength;
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
