/**
 * @file assistant.cpp
 * @brief Voice assistant orchestrator implementation
 */

#include "assistant.h"
#include "../audio/audio_player.h"
#include <LittleFS.h>

// Uncomment to play back mic recording instead of sending to Whisper
#define ASSISTANT_DEBUG_PLAYBACK

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
    , pendingMicStart(false)
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
        if (config.sttLanguage[0] != '\0') {
            sttClient.setLanguage(config.sttLanguage);
        }
    }

    // Initialize TTS client (OpenAI)
    if (strlen(config.openaiVoiceKey) > 0) {
        if (!ttsClient.begin(config.openaiVoiceKey)) {
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
            // Close the temp file and start playback
            if (ttsFile) {
                ttsFile.close();
                Serial.printf("[Assistant] TTS complete, playing %d bytes of audio\n", ttsAudioWritePos);
                audioPlayer.play(TTS_TEMP_FILE);
            } else {
                setState(AssistantState::Idle);
            }
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
    voiceInput.update(dt);
    sttClient.loop();
    ttsClient.loop();

    // Handle PTT hold detection
    if (pttActive && !pttTriggered) {
        if (millis() - pttStartTime >= ASSISTANT_PTT_HOLD_MS) {
            pttTriggered = true;
            startListening();
        }
    }

    // Start mic capture once activation chime finishes
    if (state == AssistantState::Listening && pendingMicStart) {
        if (!audioPlayer.isPlaying()) {
            pendingMicStart = false;
            sttClient.startRecording();

            // Direct audio path: capture → STT buffer (bypasses ring buffer)
            voiceInput.onAudioData([this](const uint8_t* data, size_t length) {
                sttClient.sendAudio(data, length);
            });

            voiceInput.startListening();
            listeningStartTime = millis();  // Reset so max duration counts from actual recording
            Serial.println("[Assistant] Chime done, recording...");
        }
        return;  // Don't check timeouts while waiting for chime
    }

    // Audio streams directly via callback (no polling needed)
    if (state == AssistantState::Listening) {
        // Check for max speaking duration
        if (millis() - listeningStartTime > ASSISTANT_MAX_SPEAK_MS) {
            Serial.println("[Assistant] Max speak duration reached");
            stopListening();
        }
    }

    // Check if speaking is done (TTS streaming finished AND audio playback done)
    // Grace period of 1s avoids race between TTS Complete and audioPlayer.play() starting
    if (state == AssistantState::Speaking) {
        bool ttsActive = ttsClient.isSpeaking() ||
                         ttsClient.getState() == TTSState::Requesting;
        if (!audioPlayer.isPlaying() && !ttsActive &&
            millis() - speakingStartTime > 1000) {
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
    setState(AssistantState::Listening);  // Triggers chime via onStateChange callback
    listeningStartTime = millis();

    // Defer mic capture until activation chime finishes playing
    pendingMicStart = true;
    Serial.println("[Assistant] Waiting for chime to finish before recording...");
}

void Assistant::stopListening() {
    if (state != AssistantState::Listening) return;

    // If still waiting for chime, cancel without processing
    if (pendingMicStart) {
        pendingMicStart = false;
        audioPlayer.stop();
        setState(AssistantState::Idle);
        Serial.println("[Assistant] Cancelled during chime");
        return;
    }

    voiceInput.stopListening();
    voiceInput.onAudioData(nullptr);  // Disconnect direct path
    uint32_t listenDuration = millis() - listeningStartTime;
    size_t audioBytes = sttClient.getBufferedAudioSize();
    float expectedMs = audioBytes / 32.0f;  // 16kHz × 2 bytes = 32000 bytes/sec
    Serial.printf("[Assistant] Listened for %lums, audio=%d bytes (%.0fms), ratio=%.2f\n",
                  listenDuration, audioBytes, expectedMs,
                  expectedMs > 0 ? (float)listenDuration / expectedMs : 0.0f);
    setState(AssistantState::Processing);

#ifdef ASSISTANT_DEBUG_PLAYBACK
    // Debug mode: play back the recording instead of transcribing
    sttClient.logAudioStats();
    bool saved = sttClient.saveAsWav("/debug_mic.wav");
    sttClient.cancelRecording();  // Reset STT to Idle so next recording starts with clean buffer
    if (saved) {
        Serial.println("[Assistant] Debug: playing back recording...");
        setState(AssistantState::Speaking);
        speakingStartTime = millis();
        audioPlayer.play("/debug_mic.wav");
    } else {
        setState(AssistantState::Idle);
    }
    return;
#endif

    // Stop recording and send to Whisper API
    // This triggers the transcript callback when done
    uint32_t sttStart = millis();
    if (!sttClient.stopRecording()) {
        Serial.println("[Assistant] STT failed");
        setState(AssistantState::Idle);
    } else {
        Serial.printf("[Assistant] STT took %lums\n", millis() - sttStart);
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
    uint32_t llmStart = millis();
    LLMResponse response = llmClient.send(transcript);
    uint32_t llmElapsed = millis() - llmStart;

    if (response.success) {
        Serial.printf("[Assistant] LLM took %lums (%d in / %d out tokens)\n",
                      llmElapsed, response.inputTokens, response.outputTokens);
        handleLLMResponse(response);
    } else {
        Serial.printf("[Assistant] LLM error after %lums: %s\n", llmElapsed, response.error.c_str());
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

    // Handle tool calls if any — execute tools and let the follow-up response handle speech
    if (!response.toolCalls.empty()) {
        executeToolCalls(response.toolCalls);
        return;
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
        Serial.printf("[Assistant] Response (%d chars): %.80s%s\n",
                      textToSpeak.length(), textToSpeak.c_str(),
                      textToSpeak.length() > 80 ? "..." : "");
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
    // Execute ALL tools first, collect results
    struct Result { String id; String value; };
    std::vector<Result> results;

    for (const auto& call : calls) {
        Serial.printf("[Assistant] Tool call: %s(%s)\n", call.name.c_str(), call.input.c_str());
        uint32_t toolStart = millis();
        String result = llmClient.executeTool(call.name.c_str(), call.input.c_str());
        Serial.printf("[Assistant] Tool %s took %lums → %.100s%s\n",
                      call.name.c_str(), millis() - toolStart,
                      result.c_str(), result.length() > 100 ? "..." : "");
        results.push_back({call.id, result});
    }

    // Queue all results except last into history (without sending)
    for (size_t i = 0; i + 1 < results.size(); i++) {
        llmClient.queueToolResult(results[i].id.c_str(), results[i].value.c_str());
    }

    // Send last result (triggers the API call with ALL results in history)
    uint32_t followUpStart = millis();
    LLMResponse followUp = llmClient.addToolResult(
        results.back().id.c_str(), results.back().value.c_str());

    if (followUp.success) {
        Serial.printf("[Assistant] Tool follow-up LLM took %lums\n", millis() - followUpStart);
        handleLLMResponse(followUp);
    } else {
        Serial.printf("[Assistant] Tool follow-up failed after %lums: %s\n",
                      millis() - followUpStart, followUp.error.c_str());
        setState(AssistantState::Error);
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

    // Write chunk to temp file (playback starts when onStateChange fires Complete)
    size_t written = ttsFile.write(data, length);
    ttsAudioWritePos += written;
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

    // Update STT + TTS with new voice key (always OpenAI)
    if (strlen(config.openaiVoiceKey) > 0) {
        sttClient.setApiKey(config.openaiVoiceKey);
        sttClient.setLanguage(config.sttLanguage);
        ttsClient.setApiKey(config.openaiVoiceKey);
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
