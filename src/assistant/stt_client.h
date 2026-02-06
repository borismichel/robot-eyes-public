/**
 * @file stt_client.h
 * @brief Speech-to-text client using OpenAI Whisper API
 *
 * Buffers audio during recording and sends to OpenAI Whisper API
 * for transcription when recording stops.
 */

#ifndef STT_CLIENT_H
#define STT_CLIENT_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <functional>

//=============================================================================
// Configuration
//=============================================================================

/** OpenAI Whisper API endpoint */
#define WHISPER_API_HOST "api.openai.com"
#define WHISPER_API_PATH "/v1/audio/transcriptions"

/** Whisper model */
#define WHISPER_MODEL "whisper-1"

/** HTTP timeout (ms) */
#define STT_HTTP_TIMEOUT_MS 30000

/** Maximum audio buffer size (32KB = ~2 seconds at 16kHz mono) */
#define STT_MAX_AUDIO_BUFFER (32 * 1024)

/** Maximum transcript length */
#define MAX_TRANSCRIPT_LENGTH 1024

//=============================================================================
// STT State and Callbacks
//=============================================================================

/**
 * @enum STTState
 * @brief Current state of the STT client
 */
enum class STTState {
    Idle,           ///< Ready to record
    Recording,      ///< Buffering audio
    Transcribing,   ///< Sending to Whisper API
    Error           ///< Error state
};

/**
 * @brief Callback for transcription results
 * @param transcript Transcript text
 * @param isFinal Always true for Whisper (no interim results)
 */
using TranscriptCallback = std::function<void(const char* transcript, bool isFinal)>;

/**
 * @brief Callback for errors
 * @param errorMessage Error description
 */
using ErrorCallback = std::function<void(const char* errorMessage)>;

//=============================================================================
// STTClient Class
//=============================================================================

/**
 * @class STTClient
 * @brief Speech-to-text via OpenAI Whisper API
 *
 * This client buffers audio during recording, then sends to Whisper
 * for transcription when recording stops. Uses HTTP, not streaming.
 */
class STTClient {
public:
    STTClient();
    ~STTClient();

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

    //-------------------------------------------------------------------------
    // Recording Control
    //-------------------------------------------------------------------------

    /**
     * @brief Start recording audio
     * @return true if started successfully
     */
    bool startRecording();

    /**
     * @brief Stop recording and transcribe
     * @return true if transcription request sent
     */
    bool stopRecording();

    /**
     * @brief Send audio data (call repeatedly while recording)
     * @param data Audio samples (16-bit PCM, 16kHz, mono)
     * @param length Data length in bytes
     * @return true if buffered successfully
     */
    bool sendAudio(const uint8_t* data, size_t length);

    /**
     * @brief Check if recording
     */
    bool isRecording() const { return state == STTState::Recording; }

    /**
     * @brief Check if transcribing
     */
    bool isTranscribing() const { return state == STTState::Transcribing; }

    //-------------------------------------------------------------------------
    // Legacy compatibility aliases
    //-------------------------------------------------------------------------

    bool connect() { return true; }  // No-op, kept for compatibility
    void disconnect() {}  // No-op
    bool isConnected() const { return initialized; }
    void startStreaming() { startRecording(); }
    void stopStreaming() { stopRecording(); }
    bool isStreaming() const { return isRecording(); }
    void loop() {}  // No-op, no WebSocket to poll

    //-------------------------------------------------------------------------
    // Results
    //-------------------------------------------------------------------------

    /**
     * @brief Get current/last transcript
     */
    const char* getTranscript() const { return transcript; }

    /**
     * @brief Get final transcript
     */
    const char* getFinalTranscript() const { return transcript; }

    /**
     * @brief Alias for getTranscript
     */
    const char* getCurrentTranscript() const { return transcript; }

    /**
     * @brief Check if transcript is ready
     */
    bool hasFinalTranscript() const { return transcriptReady; }

    /**
     * @brief Clear transcript
     */
    void clearTranscript();

    //-------------------------------------------------------------------------
    // State
    //-------------------------------------------------------------------------

    /**
     * @brief Get current state
     */
    STTState getState() const { return state; }

    /**
     * @brief Get last error message
     */
    const char* getError() const { return lastError; }

    /**
     * @brief Get buffered audio size
     */
    size_t getBufferedAudioSize() const { return audioBufferPos; }

    //-------------------------------------------------------------------------
    // Callbacks
    //-------------------------------------------------------------------------

    /**
     * @brief Set transcript callback
     */
    void onTranscript(TranscriptCallback callback) { transcriptCallback = callback; }

    /**
     * @brief Set error callback
     */
    void onError(ErrorCallback callback) { errorCallback = callback; }

private:
    /**
     * @brief Send audio to Whisper API and get transcription
     */
    bool transcribe();

    /**
     * @brief Build WAV header for audio data
     */
    void buildWavHeader(uint8_t* header, uint32_t dataSize);

    STTState state;

    // Configuration
    char apiKey[128];
    bool initialized;

    // Audio buffer
    uint8_t* audioBuffer;
    size_t audioBufferPos;
    size_t audioBufferSize;

    // Transcript
    char transcript[MAX_TRANSCRIPT_LENGTH];
    bool transcriptReady;

    // Error handling
    char lastError[128];

    // HTTP client
    NetworkClientSecure* secureClient;
    HTTPClient http;

    // Callbacks
    TranscriptCallback transcriptCallback;
    ErrorCallback errorCallback;
};

#endif // STT_CLIENT_H
