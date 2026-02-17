/**
 * @file assistant.cpp
 * @brief Voice assistant orchestrator implementation
 */

#include "assistant.h"
#include "../audio/audio_player.h"

// Uncomment to play back mic recording instead of sending to Whisper
// #define ASSISTANT_DEBUG_PLAYBACK

// Global instance
Assistant assistant;

// External audio player reference
extern AudioPlayer audioPlayer;

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
    , processingStartTime(0)
    , ttsAudioWritePos(0)
    , ttsInterrupted(false)
    , voiceTaskHandle(nullptr)
    , voiceProcessSem(nullptr)
    , stateCallback(nullptr)
    , transcriptCallback(nullptr)
    , responseCallback(nullptr)
{
    memset(audioChunkBuffer, 0, sizeof(audioChunkBuffer));
    memset(lastResponse, 0, sizeof(lastResponse));
    memset(lastEmotion, 0, sizeof(lastEmotion));
    memset(ttsWavHeader, 0, sizeof(ttsWavHeader));
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

    // Set up callbacks (transcript callback only — processing runs on voice task)
    sttClient.onTranscript([this](const char* text, bool isFinal) {
        strncpy(lastResponse, text, sizeof(lastResponse) - 1);
        if (transcriptCallback) transcriptCallback(text, isFinal);
    });

    ttsClient.onAudioChunk([this](const uint8_t* data, size_t len) {
        handleTTSAudio(data, len);
    });

    ttsClient.onStateChange([this](TTSState ttsState) {
        if (ttsState == TTSState::Complete) {
            Serial.printf("[Assistant] TTS complete, streamed %d bytes\n", ttsAudioWritePos);
            if (audioPlayer.isStreamingPCM()) {
                audioPlayer.finishStreamingPCM();
            }
            ttsInterrupted = false;
            // Speaking→Idle: update() handles via isPlaying() + 1s grace
            // If we never reached Speaking (e.g. error), go Idle now
            if (state != AssistantState::Speaking) {
                setState(AssistantState::Idle);
            }
        }
    });

    // Create voice processing task (runs STT→LLM→TTS on background thread)
    voiceProcessSem = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(
        voiceTaskFunc,      // Task function
        "VoiceProc",        // Name
        16384,              // Stack (16KB for JSON + HTTP)
        this,               // Parameter
        2,                  // Priority (higher than audio task)
        &voiceTaskHandle,   // Handle
        1                   // Core 1 (same as main loop, WiFi-safe)
    );

    initialized = true;
    state = AssistantState::Idle;
    Serial.println("[Assistant] Ready");
    return true;
}

void Assistant::end() {
    if (!initialized) return;

    if (voiceTaskHandle) {
        vTaskDelete(voiceTaskHandle);
        voiceTaskHandle = nullptr;
    }
    if (voiceProcessSem) {
        vSemaphoreDelete(voiceProcessSem);
        voiceProcessSem = nullptr;
    }

    sttClient.end();
    ttsClient.end();
    llmClient.end();
    voiceInput.end();

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

    // Safety timeout: recover from stuck Processing state (STT/LLM/TTS hung)
    if (state == AssistantState::Processing) {
        if (millis() - processingStartTime > 60000) {
            Serial.println("[Assistant] Processing timeout (60s), resetting to Idle");
            setState(AssistantState::Idle);
        }
    }

    // Check if speaking is done — wait for streaming/playback to finish + grace period
    if (state == AssistantState::Speaking) {
        if (!audioPlayer.isPlaying() && millis() - speakingStartTime > 1000) {
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
        if (audioPlayer.isStreamingPCM()) {
            // Streaming from voice task — can't call ttsClient.stop() (not thread-safe).
            // Set flag so handleTTSAudio discards remaining data.
            ttsInterrupted = true;
            audioPlayer.finishStreamingPCM();
        } else {
            ttsClient.stop();
            audioPlayer.stop();
        }
        setState(AssistantState::Idle);
        Serial.println("[Assistant] Interrupted");
    } else if (state == AssistantState::Listening) {
        stopListening();
    }
}

bool Assistant::say(const char* text) {
    if (!initialized) {
        Serial.println("[Assistant] say: not initialized");
        return false;
    }
    if (state != AssistantState::Idle && state != AssistantState::Disabled) {
        Serial.println("[Assistant] say: busy");
        return false;
    }
    if (!text || strlen(text) == 0) {
        Serial.println("[Assistant] say: empty text");
        return false;
    }

    Serial.printf("[Assistant] say: %.80s%s\n", text, strlen(text) > 80 ? "..." : "");

    // Stream TTS audio directly to I2S (no temp file).
    // speak() blocks — handleTTSAudio streams PCM as chunks arrive.
    ttsAudioWritePos = 0;
    ttsInterrupted = false;
    ttsClient.speak(text);

    return true;
}

void Assistant::startListening() {
    if (state != AssistantState::Idle) return;
    memset(lastResponse, 0, sizeof(lastResponse));  // Clear for transcript display
    llmClient.clearHistory();  // Fresh conversation — avoids stale tool_use_id errors
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
    processingStartTime = millis();

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

    // Signal voice processing task to run STT→LLM→TTS in background
    // (main loop continues to update display, animations, expressions)
    xSemaphoreGive(voiceProcessSem);
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
    String speakableText = response.text;

    // Extract and store emotion
    if (!response.emotion.isEmpty()) {
        strncpy(lastEmotion, response.emotion.c_str(), sizeof(lastEmotion) - 1);
    }

    // Handle tool calls with at most ONE follow-up
    if (!response.toolCalls.empty()) {
        // Execute all tools and queue results (no LLM call yet)
        executeToolCalls(response.toolCalls);

        // Check if we got speakable text alongside the tools
        String checkText = speakableText;
        if (checkText.startsWith("[")) {
            int eb = checkText.indexOf(']');
            if (eb > 0) checkText = checkText.substring(eb + 1);
            checkText.trim();
        }

        if (checkText.length() == 0) {
            // No text — follow up until we get text or exhaust retries
            for (int followUp = 0; followUp < 3; followUp++) {
                uint32_t followUpStart = millis();
                LLMResponse fu = llmClient.sendFollowUp();
                Serial.printf("[Assistant] Tool follow-up #%d LLM took %lums\n", followUp + 1, millis() - followUpStart);

                if (!fu.success) break;

                if (!fu.emotion.isEmpty()) {
                    strncpy(lastEmotion, fu.emotion.c_str(), sizeof(lastEmotion) - 1);
                }

                if (!fu.toolCalls.empty()) {
                    executeToolCalls(fu.toolCalls);
                }

                // Check if we got text this round
                String fuText = fu.text;
                if (fuText.startsWith("[")) {
                    int eb = fuText.indexOf(']');
                    if (eb > 0) fuText = fuText.substring(eb + 1);
                    fuText.trim();
                }

                if (fuText.length() > 0) {
                    speakableText = fu.text;
                    break;
                }

                // No text and no more tools — LLM is done without answering
                if (fu.toolCalls.empty()) break;
            }
        }
    }

    // Store response
    strncpy(lastResponse, speakableText.c_str(), sizeof(lastResponse) - 1);

    // Strip emotion tag for speech
    if (speakableText.startsWith("[")) {
        int endBracket = speakableText.indexOf(']');
        if (endBracket > 0) {
            speakableText = speakableText.substring(endBracket + 1);
            speakableText.trim();
        }
    }

    if (speakableText.length() > 0) {
        Serial.printf("[Assistant] Response (%d chars): %.80s%s\n",
                      speakableText.length(), speakableText.c_str(),
                      speakableText.length() > 80 ? "..." : "");
        // Free LLM conversation history before TTS to reclaim heap for SSL
        llmClient.clearHistory();
        Serial.printf("[Assistant] Free heap before TTS: %d\n", ESP.getFreeHeap());
        playResponse(speakableText.c_str());
    } else {
        setState(AssistantState::Idle);
    }

    // Notify callback
    if (responseCallback) {
        responseCallback(lastResponse, lastEmotion);
    }
}

void Assistant::executeToolCalls(const std::vector<ToolCall>& calls) {
    for (const auto& call : calls) {
        Serial.printf("[Assistant] Tool call: %s(%s)\n", call.name.c_str(), call.input.c_str());
        uint32_t toolStart = millis();
        String result = llmClient.executeTool(call.name.c_str(), call.input.c_str());
        Serial.printf("[Assistant] Tool %s took %lums → %.100s%s\n",
                      call.name.c_str(), millis() - toolStart,
                      result.c_str(), result.length() > 100 ? "..." : "");
        llmClient.queueToolResult(call.id.c_str(), result.c_str());
    }
}

//=============================================================================
// TTS Playback
//=============================================================================
void Assistant::playResponse(const char* text) {
    // Stream TTS audio directly to I2S — no temp file needed.
    // handleTTSAudio() parses WAV header, then feeds PCM to AudioPlayer.
    // Speaking state is set as soon as the WAV header is parsed (~first chunk).
    ttsAudioWritePos = 0;
    ttsInterrupted = false;

    // speak() blocks while audio streams to I2S via handleTTSAudio callback.
    // The voice task is blocked, but the main loop continues (FreeRTOS scheduling).
    ttsClient.speak(text);
}

void Assistant::handleTTSAudio(const uint8_t* data, size_t length) {
    if (ttsInterrupted) return;

    // Buffer WAV header (first 44 bytes), then stream remaining PCM to I2S
    while (ttsAudioWritePos < 44 && length > 0) {
        size_t need = 44 - ttsAudioWritePos;
        size_t toCopy = (length < need) ? length : need;
        memcpy(ttsWavHeader + ttsAudioWritePos, data, toCopy);
        ttsAudioWritePos += toCopy;
        data += toCopy;
        length -= toCopy;

        if (ttsAudioWritePos >= 44) {
            // Parse WAV header fields
            int sampleRate;
            int16_t channels, bitsPerSample;
            memcpy(&sampleRate, ttsWavHeader + 24, 4);
            memcpy(&channels, ttsWavHeader + 22, 2);
            memcpy(&bitsPerSample, ttsWavHeader + 34, 2);

            Serial.printf("[Assistant] TTS WAV: %dHz %dch %dbit\n",
                          sampleRate, (int)channels, (int)bitsPerSample);

            audioPlayer.startStreamingPCM(sampleRate, channels, bitsPerSample);
            setState(AssistantState::Speaking);
            speakingStartTime = millis();
        }
    }

    // Stream PCM data directly to I2S (blocks on DMA when buffer full)
    if (length > 0 && audioPlayer.isStreamingPCM()) {
        audioPlayer.feedPCMBytes(data, length);
        ttsAudioWritePos += length;
    }
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

//=============================================================================
// Voice Processing Background Task
//=============================================================================
void Assistant::voiceTaskFunc(void* param) {
    Assistant* self = (Assistant*)param;
    while (true) {
        // Block until stopListening() signals us
        xSemaphoreTake(self->voiceProcessSem, portMAX_DELAY);
        self->processVoicePipeline();
    }
}

void Assistant::processVoicePipeline() {
    // Runs on background task — main loop continues updating display
    Serial.printf("[Assistant] Free heap at pipeline start: %d\n", ESP.getFreeHeap());

    // 1. Send buffered audio to Whisper for transcription (blocking HTTP)
    uint32_t sttStart = millis();
    bool sttOk = sttClient.stopRecording();
    Serial.printf("[Assistant] STT took %lums\n", millis() - sttStart);

    if (!sttOk) {
        Serial.println("[Assistant] STT failed");
        setState(AssistantState::Idle);
        return;
    }

    // 2. Process transcript → LLM → TTS (all blocking)
    processTranscript();
}
