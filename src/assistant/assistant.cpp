/**
 * @file assistant.cpp
 * @brief Voice assistant orchestrator implementation
 */

#include "assistant.h"
#include "../audio/audio_player.h"
#include <LittleFS.h>

// Global instance
Assistant assistant;

// Temporary file for TTS audio buffering
static const char* TTS_TEMP_FILE = "/tts_response.mp3";

// External audio player reference
extern AudioPlayer audioPlayer;

// TTS audio buffer size (128KB for buffering TTS response)
#define TTS_BUFFER_SIZE (128 * 1024)

// File handle for TTS temp file
static File ttsFile;

//=============================================================================
// Constructor / Destructor
//=============================================================================
Assistant::Assistant()
    : state(AssistantState::Disabled)
    , initialized(false)
    , pttActive(false)
    , pttStartTime(0)
    , pttTriggered(false)
    , listeningStartTime(0)
    , speakingStartTime(0)
    , ttsAudioBuffer(nullptr)
    , ttsAudioSize(0)
    , ttsAudioWritePos(0)
    , stateCallback(nullptr)
    , transcriptCallback(nullptr)
    , responseCallback(nullptr)
{
    memset(audioChunkBuffer, 0, sizeof(audioChunkBuffer));
    memset(lastResponse, 0, sizeof(lastResponse));
    memset(lastEmotion, 0, sizeof(lastEmotion));
}

Assistant::~Assistant() {
    end();
}

//=============================================================================
// Initialization
//=============================================================================
bool Assistant::begin(const AssistantConfig& cfg) {
    if (initialized) return true;

    config = cfg;
    Serial.println("[Assistant] Initializing...");

    // Initialize voice input
    if (!voiceInput.begin()) {
        Serial.println("[Assistant] Failed to init voice input");
        return false;
    }

    // Initialize STT client (OpenAI Whisper)
    if (strlen(config.openaiVoiceKey) > 0) {
        if (!sttClient.begin(config.openaiVoiceKey)) {
            Serial.println("[Assistant] Failed to init STT client");
        }
    }

    // Initialize TTS client (OpenAI)
    if (strlen(config.openaiVoiceKey) > 0) {
        if (!ttsClient.begin(TTSProvider::OpenAI, config.openaiVoiceKey)) {
            Serial.println("[Assistant] Failed to init TTS client");
        }
        ttsClient.setVoice(config.voiceConfig);
    }

    // Initialize LLM client (Claude or OpenAI)
    if (strlen(config.llmApiKey) > 0) {
        if (!llmClient.begin(config.llmApiKey, config.llmProvider)) {
            Serial.println("[Assistant] Failed to init LLM client");
        }
        if (strlen(config.systemPrompt) > 0) {
            llmClient.setSystemPrompt(config.systemPrompt);
        }
    }

    // Allocate TTS buffer
    ttsAudioBuffer = (uint8_t*)malloc(TTS_BUFFER_SIZE);
    if (!ttsAudioBuffer) {
        Serial.println("[Assistant] Failed to allocate TTS buffer");
    }

    // Set up callbacks
    sttClient.onTranscript([this](const char* text, bool isFinal) {
        strncpy(lastResponse, text, sizeof(lastResponse) - 1);
        if (transcriptCallback) transcriptCallback(text, isFinal);
        if (isFinal) processTranscript();
    });

    ttsClient.onAudioChunk([this](const uint8_t* data, size_t len) {
        handleTTSAudio(data, len);
    });

    ttsClient.onStateChange([this](TTSState ttsState) {
        if (ttsState == TTSState::Complete) {
            setState(AssistantState::Idle);
        }
    });

    initialized = true;
    state = AssistantState::Idle;
    Serial.println("[Assistant] Ready");
    return true;
}

void Assistant::end() {
    if (!initialized) return;

    sttClient.end();
    ttsClient.end();
    llmClient.end();
    voiceInput.end();

    if (ttsAudioBuffer) {
        free(ttsAudioBuffer);
        ttsAudioBuffer = nullptr;
    }

    initialized = false;
    state = AssistantState::Disabled;
    Serial.println("[Assistant] Shutdown");
}

//=============================================================================
// Main Update Loop
//=============================================================================
void Assistant::update(float dt) {
    if (!initialized || state == AssistantState::Disabled) return;

    // Update components
    sttClient.loop();
    ttsClient.loop();

    // Handle PTT hold detection
    if (pttActive && !pttTriggered) {
        if (millis() - pttStartTime >= ASSISTANT_PTT_HOLD_MS) {
            pttTriggered = true;
            startListening();
        }
    }

    // Stream audio while listening
    if (state == AssistantState::Listening) {
        streamAudioToSTT();

        // Check for max speaking duration
        if (millis() - listeningStartTime > ASSISTANT_MAX_SPEAK_MS) {
            Serial.println("[Assistant] Max speak duration reached");
            stopListening();
        }
    }

    // Check for speaking timeout
    if (state == AssistantState::Speaking) {
        if (!audioPlayer.isPlaying() && !ttsClient.isSpeaking()) {
            setState(AssistantState::Idle);
        }
    }
}

//=============================================================================
// Activation
//=============================================================================
void Assistant::startPushToTalk() {
    if (state == AssistantState::Disabled) return;
    pttActive = true;
    pttStartTime = millis();
    pttTriggered = false;
    Serial.println("[Assistant] PTT started");
}

void Assistant::endPushToTalk() {
    if (!pttActive) return;
    pttActive = false;
    if (pttTriggered && state == AssistantState::Listening) {
        stopListening();
    }
    pttTriggered = false;
    Serial.println("[Assistant] PTT ended");
}

void Assistant::onWakeWord() {
    if (state != AssistantState::Idle) return;
    Serial.println("[Assistant] Wake word detected");
    startListening();
}

void Assistant::interrupt() {
    if (state == AssistantState::Speaking) {
        ttsClient.stop();
        audioPlayer.stop();
        setState(AssistantState::Idle);
        Serial.println("[Assistant] Interrupted");
    } else if (state == AssistantState::Listening) {
        stopListening();
    }
}

void Assistant::startListening() {
    if (state != AssistantState::Idle) return;
    setState(AssistantState::Listening);
    listeningStartTime = millis();
    sttClient.startRecording();  // Start buffering audio
    voiceInput.startListening();
    Serial.println("[Assistant] Listening...");
}

void Assistant::stopListening() {
    if (state != AssistantState::Listening) return;
    voiceInput.stopListening();
    setState(AssistantState::Processing);
    Serial.println("[Assistant] Processing...");

    // Stop recording and send to Whisper API
    // This triggers the transcript callback when done
    if (!sttClient.stopRecording()) {
        Serial.println("[Assistant] STT failed");
        setState(AssistantState::Idle);
    }
}

//=============================================================================
// Audio Streaming
//=============================================================================
void Assistant::streamAudioToSTT() {
    size_t available = voiceInput.available();
    if (available == 0) return;

    size_t toRead = min(available, sizeof(audioChunkBuffer));
    size_t bytesRead = voiceInput.read(audioChunkBuffer, toRead);

    if (bytesRead > 0 && sttClient.isRecording()) {
        sttClient.sendAudio(audioChunkBuffer, bytesRead);
    }
}

//=============================================================================
// Processing
//=============================================================================
void Assistant::processTranscript() {
    const char* transcript = sttClient.getFinalTranscript();
    if (!transcript || strlen(transcript) == 0) {
        Serial.println("[Assistant] Empty transcript");
        setState(AssistantState::Idle);
        return;
    }

    Serial.printf("[Assistant] Transcript: %s\n", transcript);

    // Send to LLM
    LLMResponse response = llmClient.send(transcript);

    if (response.success) {
        handleLLMResponse(response);
    } else {
        Serial.printf("[Assistant] LLM error: %s\n", response.error.c_str());
        setState(AssistantState::Error);
    }
}

void Assistant::handleLLMResponse(const LLMResponse& response) {
    // Store response
    strncpy(lastResponse, response.text.c_str(), sizeof(lastResponse) - 1);

    // Extract and store emotion
    if (!response.emotion.isEmpty()) {
        strncpy(lastEmotion, response.emotion.c_str(), sizeof(lastEmotion) - 1);
    }

    // Handle tool calls if any
    if (!response.toolCalls.empty()) {
        executeToolCalls(response.toolCalls);
    }

    // Speak the response (strip emotion tag if present)
    String textToSpeak = response.text;
    if (textToSpeak.startsWith("[")) {
        int endBracket = textToSpeak.indexOf(']');
        if (endBracket > 0) {
            textToSpeak = textToSpeak.substring(endBracket + 1);
            textToSpeak.trim();
        }
    }

    if (textToSpeak.length() > 0) {
        playResponse(textToSpeak.c_str());
    } else {
        setState(AssistantState::Idle);
    }

    // Notify callback
    if (responseCallback) {
        responseCallback(lastResponse, lastEmotion);
    }
}

void Assistant::executeToolCalls(const std::vector<ToolCall>& calls) {
    // Tool execution will be implemented in Phase 3
    for (const auto& call : calls) {
        Serial.printf("[Assistant] Tool call: %s\n", call.name.c_str());
    }
}

//=============================================================================
// TTS Playback
//=============================================================================
void Assistant::playResponse(const char* text) {
    setState(AssistantState::Speaking);
    speakingStartTime = millis();

    // Reset TTS buffer
    ttsAudioWritePos = 0;

    // Open temp file for writing
    ttsFile = LittleFS.open(TTS_TEMP_FILE, "w");
    if (!ttsFile) {
        Serial.println("[Assistant] Failed to open TTS temp file");
        setState(AssistantState::Error);
        return;
    }

    // Start TTS synthesis
    ttsClient.speak(text);
    Serial.println("[Assistant] Speaking...");
}

void Assistant::handleTTSAudio(const uint8_t* data, size_t length) {
    if (!ttsFile) return;

    // Write to temp file
    size_t written = ttsFile.write(data, length);
    ttsAudioWritePos += written;

    // Check if TTS is complete
    if (ttsClient.getState() == TTSState::Complete) {
        ttsFile.close();

        // Play the temp file
        audioPlayer.play(TTS_TEMP_FILE);
    }
}

void Assistant::initTTSPlayback() {
    // Placeholder for streaming playback initialization
}

//=============================================================================
// State Management
//=============================================================================
void Assistant::setState(AssistantState newState) {
    if (state != newState) {
        state = newState;
        Serial.printf("[Assistant] State: %d\n", (int)state);
        if (stateCallback) {
            stateCallback(state);
        }
    }
}

void Assistant::setEnabled(bool enabled) {
    if (enabled && state == AssistantState::Disabled) {
        state = AssistantState::Idle;
    } else if (!enabled && state != AssistantState::Disabled) {
        interrupt();
        state = AssistantState::Disabled;
    }
}

void Assistant::setConfig(const AssistantConfig& cfg) {
    config = cfg;

    // Update TTS with new config (always OpenAI)
    if (strlen(config.openaiVoiceKey) > 0) {
        ttsClient.setApiKey(config.openaiVoiceKey);
        ttsClient.setProvider(TTSProvider::OpenAI);
        ttsClient.setVoice(config.voiceConfig);
    }

    // Update LLM with new config
    if (strlen(config.llmApiKey) > 0) {
        llmClient.setApiKey(config.llmApiKey);
        llmClient.setProvider(config.llmProvider);
        if (strlen(config.systemPrompt) > 0) {
            llmClient.setSystemPrompt(config.systemPrompt);
        }
    }
}

const char* Assistant::getCurrentTranscript() const {
    if (state == AssistantState::Listening) {
        return sttClient.getCurrentTranscript();
    }
    return lastResponse;
}
