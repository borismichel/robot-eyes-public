/**
 * @file assistant.h
 * @brief Voice assistant orchestrator
 *
 * Coordinates voice input, STT, LLM, and TTS to provide
 * a complete voice assistant experience on DeskBuddy.
 */

#ifndef ASSISTANT_H
#define ASSISTANT_H

#include <Arduino.h>
#include <functional>
#include "voice_input.h"
#include "stt_client.h"
#include "tts_client.h"
#include "llm_client.h"

//=============================================================================
// Configuration
//=============================================================================

/** Audio chunk size for STT streaming (bytes) */
#define ASSISTANT_AUDIO_CHUNK_SIZE 1024

/** Minimum audio to send before STT response expected (ms) */
#define ASSISTANT_MIN_AUDIO_MS 500

/** Hold duration to trigger push-to-talk (ms) */
#define ASSISTANT_PTT_HOLD_MS 500

/** Max speaking duration before auto-stop (ms) */
#define ASSISTANT_MAX_SPEAK_MS 30000

//=============================================================================
// Assistant State
//=============================================================================

/**
 * @enum AssistantState
 * @brief Current state of the voice assistant
 */
enum class AssistantState {
    Disabled,       ///< Assistant is disabled
    Idle,           ///< Waiting for activation
    Listening,      ///< Capturing voice input
    Processing,     ///< Sending to STT/LLM
    Speaking,       ///< Playing TTS response
    Error           ///< Error occurred
};

//=============================================================================
// Callbacks
//=============================================================================

/**
 * @brief Callback for state changes
 */
using AssistantStateCallback = std::function<void(AssistantState state)>;

/**
 * @brief Callback for transcript updates
 */
using TranscriptUpdateCallback = std::function<void(const char* text, bool isFinal)>;

/**
 * @brief Callback for response ready
 */
using ResponseReadyCallback = std::function<void(const char* text, const char* emotion)>;

//=============================================================================
// AssistantConfig
//=============================================================================

/**
 * @struct AssistantConfig
 * @brief Configuration for the voice assistant
 */
struct AssistantConfig {
    // LLM settings
    LLMProvider llmProvider;        ///< Claude or OpenAI
    char llmApiKey[128];            ///< API key for LLM provider

    // Voice settings (OpenAI for both STT and TTS)
    char openaiVoiceKey[128];       ///< OpenAI API key for Whisper STT and TTS
    VoiceConfig voiceConfig;        ///< Voice selection and speed

    // Activation settings
    bool wakeWordEnabled;
    bool pushToTalkEnabled;
    float wakeWordSensitivity;

    // System prompt
    char systemPrompt[1024];

    AssistantConfig() {
        llmProvider = LLMProvider::Claude;
        memset(llmApiKey, 0, sizeof(llmApiKey));
        memset(openaiVoiceKey, 0, sizeof(openaiVoiceKey));

        wakeWordEnabled = true;
        pushToTalkEnabled = true;
        wakeWordSensitivity = 0.5f;

        memset(systemPrompt, 0, sizeof(systemPrompt));
    }
};

//=============================================================================
// Assistant Class
//=============================================================================

/**
 * @class Assistant
 * @brief Main voice assistant orchestrator
 *
 * Manages the complete voice assistant pipeline:
 * 1. Activation (wake word or push-to-talk)
 * 2. Voice capture and streaming to STT
 * 3. Sending transcript to Claude LLM
 * 4. Playing TTS response
 */
class Assistant {
public:
    Assistant();
    ~Assistant();

    /**
     * @brief Initialize the assistant
     * @param config Assistant configuration
     * @return true if initialization successful
     */
    bool begin(const AssistantConfig& config);

    /**
     * @brief Cleanup
     */
    void end();

    /**
     * @brief Main update loop (call from loop)
     * @param dt Delta time in seconds
     */
    void update(float dt);

    //-------------------------------------------------------------------------
    // Activation
    //-------------------------------------------------------------------------

    /**
     * @brief Start push-to-talk activation
     * Call when user starts holding the screen
     */
    void startPushToTalk();

    /**
     * @brief End push-to-talk
     * Call when user releases the screen
     */
    void endPushToTalk();

    /**
     * @brief Handle wake word detection
     */
    void onWakeWord();

    /**
     * @brief Interrupt current action (tap while speaking)
     */
    void interrupt();

    //-------------------------------------------------------------------------
    // State
    //-------------------------------------------------------------------------

    /**
     * @brief Get current state
     */
    AssistantState getState() const { return state; }

    /**
     * @brief Check if enabled
     */
    bool isEnabled() const { return state != AssistantState::Disabled; }

    /**
     * @brief Check if speaking
     */
    bool isSpeaking() const { return state == AssistantState::Speaking; }

    /**
     * @brief Check if listening
     */
    bool isListening() const { return state == AssistantState::Listening; }

    /**
     * @brief Get current transcript (during listening)
     */
    const char* getCurrentTranscript() const;

    /**
     * @brief Get last response
     */
    const char* getLastResponse() const { return lastResponse; }

    /**
     * @brief Get last emotion
     */
    const char* getLastEmotion() const { return lastEmotion; }

    //-------------------------------------------------------------------------
    // Configuration
    //-------------------------------------------------------------------------

    /**
     * @brief Update configuration
     */
    void setConfig(const AssistantConfig& config);

    /**
     * @brief Get current configuration
     */
    const AssistantConfig& getConfig() const { return config; }

    /**
     * @brief Enable/disable the assistant
     */
    void setEnabled(bool enabled);

    //-------------------------------------------------------------------------
    // Callbacks
    //-------------------------------------------------------------------------

    /**
     * @brief Set state change callback
     */
    void onStateChange(AssistantStateCallback callback) { stateCallback = callback; }

    /**
     * @brief Set transcript update callback
     */
    void onTranscriptUpdate(TranscriptUpdateCallback callback) { transcriptCallback = callback; }

    /**
     * @brief Set response ready callback
     */
    void onResponseReady(ResponseReadyCallback callback) { responseCallback = callback; }

    //-------------------------------------------------------------------------
    // Components (for advanced use)
    //-------------------------------------------------------------------------

    /**
     * @brief Get LLM client for tool management
     */
    LLMClient& getLLM() { return llmClient; }

    /**
     * @brief Get voice input for level monitoring
     */
    VoiceInput& getVoiceInput() { return voiceInput; }

private:
    /**
     * @brief Set state and notify callback
     */
    void setState(AssistantState newState);

    /**
     * @brief Stream audio to STT
     */
    void streamAudioToSTT();

    /**
     * @brief Process STT result and send to LLM
     */
    void processTranscript();

    /**
     * @brief Handle LLM response
     */
    void handleLLMResponse(const LLMResponse& response);

    /**
     * @brief Play TTS response
     */
    void playResponse(const char* text);

    /**
     * @brief Handle TTS audio chunk
     */
    void handleTTSAudio(const uint8_t* data, size_t length);

    /**
     * @brief Initialize TTS audio playback
     */
    void initTTSPlayback();

    /**
     * @brief Execute tool calls from LLM
     */
    void executeToolCalls(const std::vector<ToolCall>& calls);

    /**
     * @brief Start listening for voice input
     */
    void startListening();

    /**
     * @brief Stop listening and process
     */
    void stopListening();

    // State
    AssistantState state;
    AssistantConfig config;
    bool initialized;

    // Components
    VoiceInput voiceInput;
    STTClient sttClient;
    TTSClient ttsClient;
    LLMClient llmClient;

    // PTT tracking
    bool pttActive;
    uint32_t pttStartTime;
    bool pttTriggered;

    // Audio streaming
    uint8_t audioChunkBuffer[ASSISTANT_AUDIO_CHUNK_SIZE];
    uint32_t listeningStartTime;

    // Response tracking
    char lastResponse[1024];
    char lastEmotion[32];
    uint32_t speakingStartTime;

    // MP3 playback buffer
    uint8_t* ttsAudioBuffer;
    size_t ttsAudioSize;
    size_t ttsAudioWritePos;

    // Callbacks
    AssistantStateCallback stateCallback;
    TranscriptUpdateCallback transcriptCallback;
    ResponseReadyCallback responseCallback;
};

// Global assistant instance
extern Assistant assistant;

#endif // ASSISTANT_H
