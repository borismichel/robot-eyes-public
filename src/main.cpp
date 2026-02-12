/**
 * Robot Eyes - Expressive eye animation with parametric shapes
 * Uses EyeShape for flexible expressions, Tweener for smooth transitions,
 * IdleBehavior for autonomous lifelike movements, and EyeRenderer for framebuffer output
 */

#include <Arduino.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include "pin_config.h"
#include "eyes/eye_shape.h"
#include "eyes/eye_renderer.h"
#include "display/screen_compositor.h"
#include "eyes/eye_animator.h"
#include "animation/tweener.h"
#include "animation/blink_controller.h"
#include "behavior/expressions.h"
#include "behavior/expression_manager.h"
#include "behavior/micro_expressions.h"
#include "behavior/reactive_animations.h"
#include "behavior/idle_behavior.h"
#include "input/imu_handler.h"
#include "input/audio_handler.h"
#include "behavior/sleep_behavior.h"
#include "behavior/time_mood.h"
#include "audio/audio_player.h"
#include "ui/settings_menu.h"
#include "ui/pomodoro.h"
#include "ui/countdown_timer.h"
#include "ui/timer_visuals.h"
#include "ui/reminder_manager.h"
#include "network/wifi_manager.h"
#include "network/web_server.h"
#include "network/captive_portal.h"
#include "network/network_setup.h"
#include "network/ota_manager.h"
#include "behavior/breathing_exercise.h"
#include "behavior/mood_drift.h"
#include "assistant/mcp_server.h"
#include "assistant/device_tools.h"
#include "assistant/mcp_client.h"
#include "assistant/assistant.h"

#define SCREEN_WIDTH  368
#define SCREEN_HEIGHT 448
#define TOUCH_ADDR    0x38

// Touch gesture thresholds (ms)
#define TAP_MAX_DURATION    300   // Max duration for a tap
#define HOLD_MIN_DURATION   500   // Min duration to count as hold
#define PET_MIN_DURATION    2000  // Min duration to trigger petting response

// Touch gesture types
enum class TouchGesture {
    None,
    Tap,
    Hold,
    Pet
};

ScreenCompositor compositor;

EyeAnimator eyeAnimator;
EyeRenderer renderer;
IdleBehavior idle;
ImuHandler imu;
AudioHandler audio;
SleepBehavior sleepBehavior;
AudioPlayer audioPlayer;
SettingsMenu settingsMenu;
PomodoroTimer pomodoroTimer;
CountdownTimer countdownTimer;
WiFiManager wifiManager;
WebServerManager webServer;
CaptivePortal captivePortal;
OtaManager otaManager;
BreathingExercise breathingExercise;
TimerVisuals timerVisuals;
NetworkSetup networkSetup;
ReminderManager reminderManager;
MoodDrift moodDrift;

// Expression manager (priority-based expression control)
ExpressionManager expressionManager;

// Expression tokens for each subsystem
uint8_t baseToken = EXPR_TOKEN_INVALID;

uint8_t gestureToken = EXPR_TOKEN_INVALID;
uint8_t assistantToken = EXPR_TOKEN_INVALID;
uint8_t sleepToken = EXPR_TOKEN_INVALID;
uint8_t previewToken = EXPR_TOKEN_INVALID;

// Frame timing
uint32_t lastFrameTime = 0;
float deltaTime = 0.016f;  // 60fps default

// Blink controller (driven by IdleBehavior)
BlinkController blinkController;

// Touch state
int16_t touchX = -1, touchY = -1;
bool isTouching = false;
uint32_t lastTouchTime = 0;
uint32_t touchStartTime = 0;
bool wasTouching = false;
bool isPetted = false;              // Currently being petted
// Double-tap detection for settings menu
uint32_t lastTapTime = 0;
const uint32_t DOUBLE_TAP_WINDOW = 350;  // Max time between taps for double-tap


// Petting brightness pulse
float pettingPulsePhase = 0.0f;
bool pettingSoundPlayed = false;  // Track if we've played the happy sound

// Sleep wake tracking
bool wasSleepingOrDrowsy = false;  // Track previous sleep state for wake detection


// Time-of-day mood system
TimeMood currentMood = TimeMood::Afternoon;
MoodModifiers moodModifiers = {1.0f, 1.0f, 0.0f, "Afternoon"};




// Periodic time display
uint32_t lastTimeTick = 0;              // When we last advanced the clock
bool isShowingTime = false;             // Currently showing time overlay
uint32_t timeDisplayStart = 0;          // When the current time display started
const uint32_t TIME_DISPLAY_DURATION = 3000;  // Show time for 3 seconds
const uint32_t TIME_TICK_INTERVAL = 60000;    // Advance clock every 60 seconds


// Micro-expression system (idle personality)
MicroExpressionSystem microExpressions;
ReactiveAnimations reactiveAnims;

// Expression timeout
const uint32_t EXPRESSION_TIMEOUT = 5000;  // 5 seconds

// Gaze tracking with tweeners


// Display driver
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3
);

Arduino_SH8601 *gfx = new Arduino_SH8601(
    bus, -1, 0, LCD_WIDTH, LCD_HEIGHT
);

// Web expression preview callback
void onWebExpressionPreview(int index) {
    if (index >= 0 && index < (int)Expression::COUNT) {
        if (previewToken != EXPR_TOKEN_INVALID) {
            expressionManager.release(previewToken);
        }
        previewToken = expressionManager.request((Expression)index, ExprPriority::Behavior);
        moodDrift.notifyActivity(millis());
        Serial.printf("Web expression preview: %s\n", getExpressionName((Expression)index));
    }
}

// Web audio test callback
void onWebAudioTest() {
    audioPlayer.play("/happy.mp3");
    Serial.println("Web audio test: playing happy.mp3");
}

// Web mood getter callback
const char* getCurrentMood() {
    return expressionManager.getCurrentName();
}

TouchGesture detectGesture() {
    if (!wasTouching) return TouchGesture::None;

    uint32_t duration = millis() - touchStartTime;

    if (duration >= PET_MIN_DURATION) {
        return TouchGesture::Pet;
    } else if (duration >= HOLD_MIN_DURATION) {
        return TouchGesture::Hold;
    } else if (duration < TAP_MAX_DURATION) {
        return TouchGesture::Tap;
    }
    return TouchGesture::None;
}

bool readTouch() {
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(0x02);
    if (Wire.endTransmission() != 0) return false;

    Wire.requestFrom(TOUCH_ADDR, 5);
    if (Wire.available() < 5) return false;

    uint8_t touchCount = Wire.read() & 0x0F;
    uint32_t now = millis();

    // Read first touch point data
    uint8_t xh = Wire.read();
    uint8_t xl = Wire.read();
    uint8_t yh = Wire.read();
    uint8_t yl = Wire.read();

    // If reminder is showing, handle Snooze/OK buttons
    if (reminderManager.isShowing()) {
        if (touchCount == 0 && wasTouching) {
            int16_t rawY = ((yh & 0x0F) << 8) | yl;
            // Left half (rawY >= 224) = SNOOZE, Right half (rawY < 224) = OK
            if (rawY >= 224) {
                reminderManager.snooze();
                Serial.println("Reminder: Snooze tapped (left half)");
            } else {
                reminderManager.dismiss();
                Serial.println("Reminder: OK tapped (right half)");
            }
        }
        isTouching = touchCount > 0;
        wasTouching = isTouching;
        return false;
    }

    // If breathing exercise prompt is showing, handle Start/Skip buttons
    if (breathingExercise.isShowingPrompt()) {
        if (touchCount == 0 && wasTouching) {
            // Touch released - check which button was tapped
            // Screen is 416 wide (after rotation), left half = Start, right half = Skip
            // After 90° CCW rotation: physical Y (0-448) maps to visual X (right-left inverted)
            // Physical Y=0 → Visual Right, Physical Y=448 → Visual Left
            int16_t rawY = ((yh & 0x0F) << 8) | yl;
            Serial.printf("Breathing touch: rawY=%d (threshold=224)\n", rawY);
            // Left half (rawY >= 224) = START, Right half (rawY < 224) = SKIP
            if (rawY >= 224) {
                breathingExercise.start();
                Serial.println("Breathing: Start tapped (left half)");
            } else {
                breathingExercise.skip();
                Serial.println("Breathing: Skip tapped (right half)");
            }
        }
        isTouching = touchCount > 0;
        wasTouching = isTouching;
        return false;
    }

    // If expired timer is flashing, tap to dismiss
    if (countdownTimer.getState() == CountdownState::Expired) {
        if (touchCount == 0 && wasTouching) {
            countdownTimer.stop();
            Serial.println("Timer: Expired flash dismissed by tap");
        }
        isTouching = touchCount > 0;
        wasTouching = isTouching;
        return false;
    }

    // If settings menu is open, let it handle touches
    if (settingsMenu.isOpen()) {
        int16_t screenX = touchCount > 0 ? (((xh & 0x0F) << 8) | xl) : -1;
        int16_t screenY = touchCount > 0 ? (((yh & 0x0F) << 8) | yl) : -1;
        settingsMenu.handleTouch(touchCount > 0, screenX, screenY);
        isTouching = false;
        wasTouching = false;
        return false;
    }

    if (touchCount == 0) {
        // Touch released
        if (wasTouching) {
            TouchGesture gesture = detectGesture();

            if (gesture == TouchGesture::Tap) {
                // Check for double-tap (opens settings menu)
                uint32_t tapDelta = now - lastTapTime;
                Serial.printf("Tap detected. Delta: %lu ms, lastTapTime: %lu\n", tapDelta, lastTapTime);
                if (tapDelta < DOUBLE_TAP_WINDOW && lastTapTime > 0) {
                    // Double-tap detected - toggle settings menu
                    Serial.println("Double-tap detected - toggling settings menu");
                    settingsMenu.toggle();
                    lastTapTime = 0;  // Reset to prevent immediate re-trigger
                } else {
                    // Single tap - toggle voice assistant
                    lastTapTime = now;
                    if (assistant.isEnabled()) {
                        AssistantState aState = assistant.getState();
                        if (aState == AssistantState::Idle) {
                            assistant.startListening();
                            moodDrift.notifyActivity(now);
                            Serial.println("Tap: start listening");
                        } else if (aState == AssistantState::Listening) {
                            assistant.stopListening();
                            Serial.println("Tap: stop listening");
                        } else if (aState == AssistantState::Speaking) {
                            assistant.interrupt();
                            Serial.println("Tap: interrupt speaking");
                        }
                    } else {
                        Serial.println("Tap: assistant not configured");
                    }
                }
            }

            // Transition to Love hearts after petting ends
            if (isPetted) {
                expressionManager.release(gestureToken);
                gestureToken = EXPR_TOKEN_INVALID;
                reactiveAnims.triggerLove(now);
                isPetted = false;
            }
        }

        isTouching = false;
        wasTouching = false;
        return false;
    }

    touchX = ((xh & 0x0F) << 8) | xl;
    touchY = ((yh & 0x0F) << 8) | yl;

    if (!wasTouching) {
        // Touch just started
        touchStartTime = now;
    } else {
        // Ongoing touch - check for petting
        uint32_t duration = now - touchStartTime;

        if (!isPetted && duration >= PET_MIN_DURATION) {
            // Transition to content petting expression (half-moon arches)
            isPetted = true;
            reactiveAnims.cancelJoy();
            // Cancel any active micro-expression/mood - petting resets to happy idle
            microExpressions.cancel();
            pettingPulsePhase = 0.0f;
            reactiveAnims.resetBounce();
            pettingSoundPlayed = false;  // Reset so we can play
            gestureToken = expressionManager.request(Expression::ContentPetting, ExprPriority::Gesture);
            Serial.println("Petting detected!");

            // Play happy sound
            if (audioPlayer.play("/happy.mp3")) {
                pettingSoundPlayed = true;
                Serial.println("Playing happy.mp3");
            }
        }
    }

    isTouching = true;
    wasTouching = true;
    lastTouchTime = now;
    return true;
}

/**
 * @brief Wire up assistant callbacks, device tools, MCP tools, and tool executor.
 *        Called after assistant.begin() succeeds — both at boot and on hot-reload from web UI.
 */
void setupAssistantIntegration() {
    // Expression changes based on assistant state
    assistant.onStateChange([](AssistantState state) {
        switch (state) {
            case AssistantState::Listening:
                assistantToken = expressionManager.request(Expression::Listening, ExprPriority::Assistant);
                audioPlayer.play("/listen.mp3");
                break;
            case AssistantState::Processing:
                if (assistantToken != EXPR_TOKEN_INVALID) {
                    expressionManager.update(assistantToken, Expression::Thinking);
                }
                break;
            case AssistantState::Speaking:
                if (assistantToken != EXPR_TOKEN_INVALID) {
                    expressionManager.update(assistantToken, Expression::Happy);
                }
                break;
            case AssistantState::Idle:
                if (expressionManager.isActive(assistantToken)) {
                    expressionManager.release(assistantToken);
                    assistantToken = EXPR_TOKEN_INVALID;
                }
                break;
            case AssistantState::Error:
                if (assistantToken != EXPR_TOKEN_INVALID) {
                    expressionManager.update(assistantToken, Expression::Confused);
                } else {
                    assistantToken = expressionManager.request(Expression::Confused, ExprPriority::Assistant);
                }
                audioPlayer.play("/confused.mp3");
                break;
            default:
                break;
        }
    });

    // Map LLM emotion tag to expression
    assistant.onResponseReady([](const char* text, const char* emotion) {
        if (emotion && strlen(emotion) > 0) {
            Expression expr = parseExpression(emotion);
            if (assistantToken != EXPR_TOKEN_INVALID) {
                expressionManager.update(assistantToken, expr);
            }
        }
    });

    // Register built-in device tools with LLM
    registerDeviceTools(assistant.getLLM());
    Serial.printf("Registered %d device tools with LLM\n", assistant.getLLM().getToolCount());

    // Initialize MCP client and discover tools from configured servers
    mcpClient.begin();
    if (mcpClient.getServerCount() > 0) {
        int toolCount = mcpClient.discoverTools();
        Serial.printf("MCP Client: discovered %d tools from %d servers\n",
                      toolCount, mcpClient.getServerCount());

        // Register MCP client tools with LLM
        mcpClient.registerToolsWithLLM([](const char* name, const char* desc, const char* schema) -> bool {
            return assistant.getLLM().addTool(name, desc, schema);
        });
    }

    // Set up unified tool executor: tries device tools first, falls back to MCP client
    assistant.getLLM().setToolExecutor([](const char* name, const char* input) -> String {
        String result = executeDeviceTool(name, input);
        if (result.indexOf("\"Unknown tool\"") >= 0) {
            return mcpClient.executeTool(name, input);
        }
        return result;
    });

    Serial.printf("LLM has %d total tools registered\n", assistant.getLLM().getToolCount());
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n=== Robot Eyes (Touch Response) ===");
    Serial.println("Tap to change expression, hold 2s to pet");

    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);

    if (!gfx->begin()) {
        Serial.println("Display init failed!");
        while (1) delay(1000);
    }

    gfx->setBrightness(255);
    gfx->fillScreen(BG_COLOR);

    // Initialize screen compositor (PSRAM allocation, eye positions)
    if (!compositor.begin(gfx, renderer)) {
        Serial.println("Compositor buffer alloc failed!");
        while (1) delay(1000);
    }

    // Initialize idle behavior
    idle.begin();

    // Initialize IMU
    if (imu.begin()) {
        Serial.println("IMU initialized");
        imu.setTiltGazeEnabled(true);
    } else {
        Serial.println("IMU init failed (optional)");
    }

    // Initialize audio player for MP3 playback (full-duplex I2S)
    if (audioPlayer.begin()) {
        Serial.println("Audio player initialized");
    } else {
        Serial.println("Audio player init failed (optional)");
    }

    // Initialize audio handler for microphone monitoring (shares I2S with player)
    // Must be initialized AFTER audioPlayer which sets up the I2S bus
    if (audio.begin()) {
        Serial.println("Audio handler initialized (full-duplex microphone)");
    } else {
        Serial.println("Audio handler init failed (optional)");
    }

    // Initialize sleep behavior
    sleepBehavior.begin();
    // Default timeout is 30 minutes (1800000ms) - see sleep_behavior.cpp

    // Initialize settings menu (loads saved values)
    settingsMenu.begin();

    // Initialize pomodoro timer
    pomodoroTimer.begin();
    countdownTimer.begin();

    // Initialize reminder manager
    reminderManager.begin();

    // Initialize breathing exercise
    breathingExercise.begin();

    // Apply initial settings from saved preferences
    audioPlayer.setVolume(settingsMenu.getVolume());
    gfx->setBrightness((settingsMenu.getBrightness() * 255) / 100);
    // Mic sensitivity slider controls gain (0-42dB), threshold slider controls detection level
    audioPlayer.setMicGain(settingsMenu.getMicSensitivity());
    audio.setThreshold(settingsMenu.getMicThreshold() / 100.0f);
    renderer.setColor(settingsMenu.getColorRGB565());

    Serial.println("2-finger tap to open settings menu");

    // Initialize WiFi manager and network setup
    wifiManager.begin(BOOT_BUTTON_PIN);
    networkSetup.begin(wifiManager, captivePortal, settingsMenu);

    // Initialize OTA manager (validates boot partition)
    otaManager.begin();

    // Start web server (works in both AP and STA mode)
    webServer.begin(&settingsMenu, &pomodoroTimer, &wifiManager, &otaManager);
    webServer.setExpressionCallback(onWebExpressionPreview);
    webServer.setAudioTestCallback(onWebAudioTest);
    webServer.setMoodGetterCallback(getCurrentMood);
    webServer.setBreathingExercise(&breathingExercise);
    webServer.setCountdownTimer(&countdownTimer);
    webServer.setReminderManager(&reminderManager);

    // Wire up MCP device tool callbacks
    deviceToolCallbacks.onSetExpression = [](const char* expression, int durationMs) {
        Expression expr = parseExpression(expression);
        if (previewToken != EXPR_TOKEN_INVALID) {
            expressionManager.release(previewToken);
        }
        previewToken = expressionManager.request(expr, ExprPriority::Behavior);
        moodDrift.notifyActivity(millis());
    };
    deviceToolCallbacks.onSetTimer = [](int seconds, const char* name) {
        countdownTimer.start(seconds, name);
        moodDrift.notifyActivity(millis());
    };
    deviceToolCallbacks.onCancelTimer = []() {
        countdownTimer.stop();
        moodDrift.notifyActivity(millis());
    };
    deviceToolCallbacks.onStartPomodoro = [](int workMin, int breakMin) {
        pomodoroTimer.setWorkMinutes(workMin);
        pomodoroTimer.setShortBreakMinutes(breakMin);
        pomodoroTimer.start();
        moodDrift.notifyActivity(millis());
    };
    deviceToolCallbacks.onStopPomodoro = []() {
        pomodoroTimer.stop();
        moodDrift.notifyActivity(millis());
    };
    deviceToolCallbacks.onGetDeviceInfo = []() -> String {
        JsonDocument doc;
        doc["expression"] = expressionManager.getCurrentName();
        doc["wifi_rssi"] = WiFi.RSSI();
        doc["ip"] = WiFi.localIP().toString();
        doc["uptime_seconds"] = millis() / 1000;
        doc["free_heap"] = ESP.getFreeHeap();
        doc["volume"] = settingsMenu.getVolume();
        doc["brightness"] = settingsMenu.getBrightness();
        doc["eye_color"] = COLOR_PRESET_NAMES[settingsMenu.getColorIndex()];
        doc["pomodoro_active"] = pomodoroTimer.isActive();
        if (pomodoroTimer.isActive()) {
            doc["pomodoro_remaining_seconds"] = pomodoroTimer.getRemainingSeconds();
        }
        String output;
        serializeJson(doc, output);
        return output;
    };
    deviceToolCallbacks.onPlaySound = [](const char* sound) {
        String path = "/";
        path += sound;
        path += ".mp3";
        audioPlayer.play(path.c_str());
        moodDrift.notifyActivity(millis());
    };
    deviceToolCallbacks.onSetReminder = [](int hour, int minute, const char* msg, bool recurring) -> bool {
        moodDrift.notifyActivity(millis());
        return reminderManager.add(hour, minute, msg, recurring);
    };
    deviceToolCallbacks.onCancelReminder = [](const char* msg) -> bool {
        moodDrift.notifyActivity(millis());
        return reminderManager.removeByMessage(msg);
    };
    deviceToolCallbacks.onListReminders = []() -> String {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (const auto& r : reminderManager.getReminders()) {
            JsonObject obj = arr.add<JsonObject>();
            obj["hour"] = r.hour;
            obj["minute"] = r.minute;
            obj["message"] = r.message;
            obj["recurring"] = r.recurring;
        }
        String output;
        serializeJson(doc, output);
        return output;
    };
    deviceToolCallbacks.onStartBreathing = [](const char* exercise) {
        if (exercise && strcmp(exercise, "nadi") == 0) {
            breathingExercise.setExerciseType(BreathingType::NadiShodhana);
        } else if (exercise && strcmp(exercise, "box") == 0) {
            breathingExercise.setExerciseType(BreathingType::BoxBreathing);
        }
        // If empty string, use whatever type is currently selected
        breathingExercise.triggerNow();  // Idle/Disabled → ShowingPrompt
        breathingExercise.start();       // ShowingPrompt → Confirmation → exercise
        moodDrift.notifyActivity(millis());
    };
    deviceToolCallbacks.onSetVolume = [](int volume) {
        settingsMenu.setVolume(volume);
        audioPlayer.setVolume(volume);
        moodDrift.notifyActivity(millis());
    };
    deviceToolCallbacks.onSetBrightness = [](int brightness) {
        settingsMenu.setBrightness(brightness);
        gfx->setBrightness((brightness * 255) / 100);
        moodDrift.notifyActivity(millis());
    };
    deviceToolCallbacks.onSetEyeColor = [](const char* color) -> bool {
        String colorUpper = String(color);
        colorUpper.toUpperCase();
        for (int i = 0; i < NUM_COLOR_PRESETS; i++) {
            if (colorUpper == COLOR_PRESET_NAMES[i]) {
                settingsMenu.setColorIndex(i);
                renderer.setColor(settingsMenu.getColorRGB565());
                moodDrift.notifyActivity(millis());
                return true;
            }
        }
        return false;
    };

    // Initialize voice assistant from saved settings
    {
        Preferences assistantPrefs;
        assistantPrefs.begin("assistant", true);

        AssistantConfig assistantConfig;

        String llmProv = assistantPrefs.getString("llmProv", "claude");
        assistantConfig.llmProvider = (llmProv == "openai") ? LLMProvider::OpenAI : LLMProvider::Claude;

        String llmKey = assistantPrefs.getString("llmKey", "");
        strncpy(assistantConfig.llmApiKey, llmKey.c_str(), sizeof(assistantConfig.llmApiKey) - 1);

        String voiceKey = assistantPrefs.getString("voiceKey", "");
        strncpy(assistantConfig.openaiVoiceKey, voiceKey.c_str(), sizeof(assistantConfig.openaiVoiceKey) - 1);

        String ttsVoice = assistantPrefs.getString("ttsVoice", "alloy");
        strncpy(assistantConfig.voiceConfig.openAIVoice, ttsVoice.c_str(), sizeof(assistantConfig.voiceConfig.openAIVoice) - 1);
        assistantConfig.voiceConfig.speed = assistantPrefs.getFloat("ttsSpeed", 1.0f);

        String sttLang = assistantPrefs.getString("sttLang", "");
        strncpy(assistantConfig.sttLanguage, sttLang.c_str(), sizeof(assistantConfig.sttLanguage) - 1);

        String sysPrompt = assistantPrefs.getString("sysPrompt", "You are DeskBuddy, a friendly desk companion. Always reply in the same language the user speaks. Keep responses concise and conversational.");
        strncpy(assistantConfig.systemPrompt, sysPrompt.c_str(), sizeof(assistantConfig.systemPrompt) - 1);

        assistantPrefs.end();

        // Only init if API keys are configured
        if (strlen(assistantConfig.llmApiKey) > 0 && strlen(assistantConfig.openaiVoiceKey) > 0) {
            if (assistant.begin(assistantConfig)) {
                Serial.println("Voice assistant initialized");
                setupAssistantIntegration();
            } else {
                Serial.println("Voice assistant init failed (API keys may be invalid)");
            }
        } else {
            Serial.println("Voice assistant not configured (no API keys)");
        }
    }

    // Initialize eye animator (gaze, tweeners, shape pipeline)
    eyeAnimator.begin(expressionManager, blinkController, idle, imu,
                      microExpressions, reactiveAnims, breathingExercise, sleepBehavior);

    // Initialize expression manager with tweener references from eye animator
    expressionManager.begin(eyeAnimator.getLeftTweener(), eyeAnimator.getRightTweener());

    // Initialize reactive animations (IMU, joy, irritated, love, orientation, breathing)
    reactiveAnims.begin(expressionManager, moodDrift, audioPlayer);

    // Initialize micro-expression system
    microExpressions.begin(expressionManager);

    // Initialize timer visuals (pomodoro/countdown expression effects)
    timerVisuals.begin(expressionManager, audioPlayer, reactiveAnims);

    // Start with neutral expression as base mood
    baseToken = expressionManager.request(Expression::Neutral, ExprPriority::Base);
    moodDrift.notifyActivity(millis());

    // Snap tweeners to initial state
    eyeAnimator.getLeftTweener().snapTo(expressionManager.getLeftBase());
    eyeAnimator.getRightTweener().snapTo(expressionManager.getRightBase());

    // Initial render to combined buffer
    {
        uint16_t* eyeBuffer = compositor.getBuffer();
        const EyePosition& leftEyePos = compositor.getLeftEyePos();
        const EyePosition& rightEyePos = compositor.getRightEyePos();
        compositor.clearBuffer();
        renderer.renderToBuf(eyeAnimator.getLeftEye(), eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                             leftEyePos.baseX, leftEyePos.baseY, true, false);
        renderer.renderToBuf(eyeAnimator.getRightEye(), eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                             rightEyePos.baseX, rightEyePos.baseY, false, false);
        compositor.fullBlit();
    }

    lastFrameTime = millis();
    lastTimeTick = millis();  // Initialize time tick to avoid immediate display
    Serial.println("Eyes ready!");
}

void loop() {
    uint32_t now = millis();

    // Calculate delta time
    deltaTime = (now - lastFrameTime) / 1000.0f;
    if (deltaTime < 0.001f) deltaTime = 0.001f;  // Clamp minimum
    if (deltaTime > 0.1f) deltaTime = 0.1f;      // Clamp maximum (prevent large jumps)

    // Target 30fps
    if (deltaTime < 0.033f) return;
    lastFrameTime = now;

    // Update WiFi lifecycle (state machine, NTP, captive portal, timezone, toggle)
    if (networkSetup.update()) {
        compositor.requestFullScreenClear();
    }

    // Update MCP SSE keepalive
    mcpServer.update();

    // Apply settings changes from web interface
    if (webServer.hasSettingsChange()) {
        audioPlayer.setVolume(settingsMenu.getVolume());
        gfx->setBrightness((settingsMenu.getBrightness() * 255) / 100);
        audioPlayer.setMicGain(settingsMenu.getMicSensitivity());
        audio.setThreshold(settingsMenu.getMicThreshold() / 100.0f);
        renderer.setColor(settingsMenu.getColorRGB565());
        moodDrift.notifyActivity(now);
        webServer.clearSettingsChange();
    }

    // Time tracking - advance clock every minute and trigger display
    if (now - lastTimeTick >= TIME_TICK_INTERVAL) {
        lastTimeTick = now;
        settingsMenu.tickMinute();

        // Trigger time display (unless menu is open, sleeping, or breathing exercise active)
        if (!settingsMenu.isOpen() && !sleepBehavior.isSleeping() && !breathingExercise.needsFullScreenRender()) {
            isShowingTime = true;
            timeDisplayStart = now;
            Serial.printf("Showing time: %02d:%02d\n",
                          settingsMenu.getTimeHour(), settingsMenu.getTimeMinute());
        }
    }

    // Update input
    readTouch();

    // Update time-of-day mood based on current hour
    TimeMood newMood = getTimeMood(settingsMenu.getTimeHour());
    if (newMood != currentMood) {
        currentMood = newMood;
        moodModifiers = getMoodModifiers(currentMood);
        Serial.printf("Mood changed to: %s (blink=%.2f, gaze=%.2f, lid=%.2f)\n",
                      moodModifiers.moodName, moodModifiers.blinkRateMultiplier,
                      moodModifiers.gazeSpeedMultiplier, moodModifiers.baseLidOffset);
    }

    // Apply mood modifiers to idle behavior
    idle.setMoodModifiers(moodModifiers.blinkRateMultiplier, moodModifiers.gazeSpeedMultiplier);

    // Update idle behavior (gaze scanning, micro-movements, blink timing)
    idle.update(deltaTime, isTouching);

    // Update IMU and handle events
    ImuEvent imuEvent = imu.update(deltaTime);
    if (imuEvent == ImuEvent::PickedUp && !isPetted && !reactiveAnims.isImuReacting()) {
        reactiveAnims.triggerImuReaction(imuEvent);
    } else if (imuEvent == ImuEvent::ShookHard && !isPetted) {
        reactiveAnims.triggerImuReaction(imuEvent);
    } else if (imuEvent == ImuEvent::Knocked && !isPetted) {
        reactiveAnims.triggerImuReaction(imuEvent);
    }

    // Handle orientation-based expressions (face-down, tilted long)
    reactiveAnims.orientationChanged(imu.getOrientation(), isPetted);

    // Update audio player (streams audio chunks)
    audioPlayer.update();

    // Update voice assistant
    assistant.update(deltaTime);

    // Update pomodoro timer
    bool pomodoroChanged = pomodoroTimer.update(deltaTime);
    PomodoroState pomodoroState = pomodoroTimer.getState();

    // Inform breathing exercise about pomodoro/timer state (don't trigger reminders during active timers)
    bool countdownBlocking = countdownTimer.isActive() && countdownTimer.getState() != CountdownState::Expired;
    breathingExercise.setPomodoroActive(pomodoroTimer.isActive() || countdownBlocking);
    reminderManager.setBlocked(pomodoroTimer.isActive() || countdownBlocking ||
                               breathingExercise.needsFullScreenRender() || settingsMenu.isOpen());

    // Handle pomodoro expression effects, concentrate animation, tick sounds
    if (timerVisuals.updatePomodoro(pomodoroTimer, now)) {
        compositor.lastRenderedFilledLen = -1;  // Force progress bar redraw on state change
    }

    // Update countdown timer
    bool countdownChanged = countdownTimer.update(deltaTime);
    CountdownState countdownState = countdownTimer.getState();

    // Handle countdown expression effects, dismiss logic, tick sounds
    bool shouldDismissExpired = (countdownState == CountdownState::Expired) &&
        (reminderManager.isShowing() || breathingExercise.isActive() || pomodoroTimer.isActive());
    if (timerVisuals.updateCountdown(countdownTimer, now, shouldDismissExpired)) {
        compositor.lastRenderedFilledLen = -1;  // Force progress bar redraw on state change
    }

    // Update breathing exercise
    bool breathingChanged = breathingExercise.update(deltaTime,
                                                      settingsMenu.getTimeHour(),
                                                      settingsMenu.getTimeMinute());
    // Also check for external state changes (from triggerNow, start, skip)
    if (breathingExercise.consumeExternalStateChange()) {
        breathingChanged = true;
    }
    // Reset blink state when breathing becomes active to prevent mid-blink artifacts
    if (breathingChanged && breathingExercise.isActive()) {
        blinkController.reset();
    }
    // Update reminder manager (atomic time read to avoid TOCTOU between hour/minute)
    int rmHour, rmMinute;
    bool rmTimeValid = settingsMenu.getTime(rmHour, rmMinute);
    bool reminderChanged = reminderManager.update(deltaTime, rmHour, rmMinute, rmTimeValid);
    if (reminderManager.consumeExternalStateChange()) {
        reminderChanged = true;
    }
    if (reminderChanged && reminderManager.isShowing()) {
        audioPlayer.play("/joy.mp3");
    }

    // Track tick timing for breathing atmosphere
    static uint32_t lastBreathingTickTime = 0;

    if (breathingChanged && breathingExercise.isSoundEnabled()) {
        // Play sounds on state transitions
        BreathingState breathState = breathingExercise.getState();
        switch (breathState) {
            case BreathingState::ShowingPrompt:
                if (!audioPlayer.isPlaying()) {
                    audioPlayer.play("/breathe_reminder.mp3");
                }
                break;
            case BreathingState::Confirmation:
                // Silent - let "Let's Breathe" fade in peacefully
                break;
            case BreathingState::Inhale:
            case BreathingState::HoldIn:
            case BreathingState::Exhale:
            case BreathingState::HoldOut:
                // Play tick on phase transition (text on screen makes transitions obvious)
                audioPlayer.play("/tick.mp3");
                lastBreathingTickTime = now;  // Reset tick timer on phase change
                break;
            case BreathingState::Complete:
                // Notify reactive animations of breathing completion
                reactiveAnims.notifyBreathingComplete(now, breathingExercise.isSoundEnabled());
                break;
            default:
                break;
        }
    }

    // Play tick sounds every second during active breathing phases
    // Pattern: tick on transition + tick every 1s (5 ticks per 5s phase)
    if (breathingExercise.isSoundEnabled() && breathingExercise.isActive()) {
        BreathingState breathState = breathingExercise.getState();
        // Only tick during the 4 main breathing phases (not Complete)
        if (breathState == BreathingState::Inhale ||
            breathState == BreathingState::HoldIn ||
            breathState == BreathingState::Exhale ||
            breathState == BreathingState::HoldOut) {

            if (now - lastBreathingTickTime >= 1000 && !audioPlayer.isPlaying()) {
                audioPlayer.play("/tick.mp3");
                lastBreathingTickTime = now;
            }
        }
    }

    // Update audio handler for microphone monitoring (skip while assistant is listening)
    AudioEvent audioEvent = AudioEvent::None;
    if (!assistant.isListening()) {
        audioEvent = audio.update(deltaTime);

        // Mic level debug (commented out — too verbose for normal use)
        // static uint32_t lastMicDebug = 0;
        // if (now - lastMicDebug > 1000) {
        //     Serial.printf("Mic: %.3f\n", audio.getLevel());
        //     lastMicDebug = now;
        // }

        // Handle "too loud" audio event - show irritated expression
        if (audioEvent == AudioEvent::TooLoud && !isPetted && !reactiveAnims.isImuReacting() &&
            !reactiveAnims.isLove() && !reactiveAnims.isIrritated()) {
            reactiveAnims.triggerIrritated(now);
            microExpressions.cancel();
        }
    }

    // Check for interaction/motion states
    bool hasInteraction = isTouching || (audioEvent != AudioEvent::None);
    bool hasMotion = (imuEvent == ImuEvent::PickedUp) || (imuEvent == ImuEvent::ShookHard) || (imuEvent == ImuEvent::Knocked);

    // Notify idle behavior of activity for yawn timing
    if (isTouching || hasMotion || (audioEvent != AudioEvent::None)) {
        idle.notifyActivity();
        moodDrift.notifyActivity(now);
    }

    // Track sleep state before update for wake detection
    bool wasAsleep = sleepBehavior.isSleeping() || sleepBehavior.isDrowsy();
    bool wasFallingAsleep = sleepBehavior.isFallingAsleep();

    // Update sleep behavior
    sleepBehavior.update(deltaTime, hasInteraction, hasMotion);

    // Update mood drift and keep base expression in sync
    moodDrift.update(now);
    expressionManager.update(baseToken, moodDrift.getBaseMood());

    // Play confused sound when waking from sleep/drowsy due to shaking/knocking
    bool isAwakeNow = !sleepBehavior.isSleeping() && !sleepBehavior.isDrowsy();
    if (wasAsleep && isAwakeNow) {
        moodDrift.notifyWakeFromSleep(now);
    }
    if (wasAsleep && isAwakeNow && (imuEvent == ImuEvent::ShookHard || imuEvent == ImuEvent::Knocked)) {
        audioPlayer.play("/confused.mp3");
        Serial.println("Woke from sleep by shaking/knock - playing confused.mp3");
    }

    // Play yawn sound once when entering falling asleep state
    if (!wasFallingAsleep && sleepBehavior.isFallingAsleep()) {
        audioPlayer.play("/yawn.mp3");
        Serial.println("Falling asleep - playing yawn.mp3");
    }

    // Apply brightness from settings (with listening/petting pulse override)
    int baseBrightness = (settingsMenu.getBrightness() * 255) / 100;
    if (assistant.isListening()) {
        float micLevel = assistant.getVoiceInput().getLevel();
        float pulse = 0.7f + 0.3f * micLevel;
        gfx->setBrightness((uint8_t)(baseBrightness * pulse));
    } else if (isPetted) {
        pettingPulsePhase += deltaTime;
        if (pettingPulsePhase >= 1.0f) {
            pettingPulsePhase -= 1.0f;
        }
        // Pulse around the base brightness: 85-100% of base
        float pulse = 0.85f + 0.15f * sinf(pettingPulsePhase * 2.0f * PI);
        gfx->setBrightness((uint8_t)(baseBrightness * pulse));
    } else {
        // Use brightness from settings
        gfx->setBrightness(baseBrightness);
    }

    // Handle yawn behavior (30-40 min idle) - not during breathing relaxed state
    if (idle.shouldYawn() && !isPetted && !reactiveAnims.isImuReacting() &&
        !reactiveAnims.isIrritated() && !reactiveAnims.isJoy() &&
        !reactiveAnims.isBreathingRelaxed() && !reactiveAnims.isBreathingContent()) {
        reactiveAnims.triggerYawn();
    }

    // Handle random joy behavior (every 10-30 minutes when idle, not during pomodoro or breathing)
    if (reactiveAnims.shouldTriggerJoy(now) && !sleepBehavior.isSleeping() && !sleepBehavior.isDrowsy() &&
        !isPetted && !reactiveAnims.isImuReacting() && !reactiveAnims.isIrritated() &&
        !reactiveAnims.isLove() && !pomodoroTimer.isActive() &&
        !breathingExercise.needsFullScreenRender() &&
        !reactiveAnims.isBreathingRelaxed() && !reactiveAnims.isBreathingContent()) {
        reactiveAnims.triggerJoy(now);
    }

    // Update all reactive animation timers (IMU, irritated, love, joy, breathing post-completion)
    reactiveAnims.update(deltaTime, now);

    // Update bounce animation for Content (pomodoro or breathing) or Relaxed (breathing)
    if ((expressionManager.getCurrent() == Expression::Content && (pomodoroTimer.isActive() || reactiveAnims.isBreathingContent())) ||
        (expressionManager.getCurrent() == Expression::Relaxed && reactiveAnims.isBreathingRelaxed())) {
        reactiveAnims.addBounce(deltaTime);
    }

    // Update petting bounce animation
    if (isPetted) {
        reactiveAnims.addBounce(deltaTime);
    }

    //=========================================================================
    // Micro-Expression Behavior (random idle personality moments)
    //=========================================================================

    // Trigger random micro-expression (not during breathing exercise or relaxed state)
    if (microExpressions.shouldTrigger(now) && !sleepBehavior.isSleeping() &&
        !sleepBehavior.isDrowsy() && !isPetted && !reactiveAnims.isImuReacting() &&
        !reactiveAnims.isIrritated() && !reactiveAnims.isLove() && !reactiveAnims.isJoy() &&
        !expressionManager.isActive(assistantToken) &&
        (expressionManager.getCurrent() == Expression::Neutral || expressionManager.getCurrent() == moodDrift.getBaseMood()) &&
        !breathingExercise.needsFullScreenRender() &&
        !reactiveAnims.isBreathingRelaxed() && !reactiveAnims.isBreathingContent()) {
        microExpressions.trigger();
        microExpressions.scheduleNext(now);
    }

    // Update micro-expression
    microExpressions.update(deltaTime, now);

    // Cancel micro-expression on interaction
    if (microExpressions.isActive() && (isPetted || reactiveAnims.isImuReacting() || reactiveAnims.isLove() ||
        reactiveAnims.isIrritated() || reactiveAnims.isJoy())) {
        microExpressions.cancel();
        Serial.println("Micro-expression cancelled by interaction");
    }

    // Handle sleep state transitions
    if (sleepBehavior.isWakingUp() && !reactiveAnims.isImuReacting() && !reactiveAnims.isIrritated()) {
        // Show startled expression when waking up
        if (sleepToken != EXPR_TOKEN_INVALID) {
            expressionManager.update(sleepToken, Expression::Startled);
        } else {
            sleepToken = expressionManager.request(Expression::Startled, ExprPriority::Sleep);
        }
    } else if (sleepBehavior.isDrowsy() && !isPetted && !reactiveAnims.isImuReacting() && !reactiveAnims.isIrritated()) {
        // Check for snap-wide (brief alertness during drowsy)
        if (sleepBehavior.isSnapWide()) {
            // Briefly show alert/neutral during snap-wide
            if (expressionManager.getCurrent() == Expression::Sleepy) {
                if (sleepToken != EXPR_TOKEN_INVALID) {
                    expressionManager.update(sleepToken, Expression::Neutral);
                }
            }
        } else {
            // Blend toward sleepy expression based on drowsiness level
            float drowsiness = sleepBehavior.getDrowsiness();
            if (drowsiness > 0.5f && expressionManager.getCurrent() != Expression::Sleepy) {
                if (sleepToken != EXPR_TOKEN_INVALID) {
                    expressionManager.update(sleepToken, Expression::Sleepy);
                } else {
                    sleepToken = expressionManager.request(Expression::Sleepy, ExprPriority::Sleep);
                }
            }
        }
    } else if (expressionManager.isActive(sleepToken) && !sleepBehavior.isDrowsy() && !sleepBehavior.isWakingUp()) {
        // No longer drowsy or waking — release sleep token
        expressionManager.release(sleepToken);
        sleepToken = EXPR_TOKEN_INVALID;
    }

    // Expression timeout safety - return to base mood if stuck
    // Only applies when no active behaviors are controlling the expression
    {
        Expression baseMood = moodDrift.getBaseMood();
        if (expressionManager.getCurrent() != baseMood &&
            !isPetted && !reactiveAnims.isImuReacting() && !reactiveAnims.isLove() &&
            !reactiveAnims.isJoy() && !microExpressions.isActive() &&
            !reactiveAnims.isIrritated() && !reactiveAnims.isOrientationActive() &&
            !reactiveAnims.isBreathingContent() && !reactiveAnims.isBreathingRelaxed() &&
            !timerVisuals.hasActiveExpression() &&
            !expressionManager.isActive(assistantToken) &&
            !sleepBehavior.isDrowsy() && !sleepBehavior.isWakingUp() &&
            (expressionManager.getTimeSinceChange() > EXPRESSION_TIMEOUT)) {
            Serial.printf("Expression timeout - returning to base mood: %s\n", getExpressionName(baseMood));
            // Release preview token if active
            if (expressionManager.isActive(previewToken)) {
                expressionManager.release(previewToken);
                previewToken = EXPR_TOKEN_INVALID;
            }
            // Update base mood
            expressionManager.update(baseToken, baseMood);
        }
    }

    // Determine current render mode for full-screen clear on transitions
    // Modes: 0=eyes, 1=menu, 2=countdown, 3=sleep, 4=timeDisplay, 5=wifiSetup, 6=breathingPrompt, 7=breathingActive, 8=wifiChoice
    int currentRenderMode = 0;  // Default: eyes
    if (networkSetup.isShowingSetup()) {
        currentRenderMode = 5;  // wifiSetup (first-boot info screen)
    } else if (networkSetup.isShowingChoice()) {
        currentRenderMode = 8;  // wifiChoice (wifi/offline choice screen)
    } else if (sleepBehavior.isSleeping()) {
        currentRenderMode = 3;  // sleep
    } else if (settingsMenu.isOpen()) {
        currentRenderMode = 1;  // menu
    } else if (breathingExercise.isShowingPrompt() || breathingExercise.isInConfirmation()) {
        currentRenderMode = 6;  // breathingPrompt or confirmation
    } else if (breathingExercise.isActive()) {
        currentRenderMode = 7;  // breathing exercise active (eye animation)
    } else if (pomodoroTimer.isActive() &&
               pomodoroState != PomodoroState::Celebration &&
               pomodoroState != PomodoroState::WaitingForTap &&
               !timerVisuals.isConcentrating()) {
        currentRenderMode = 2;  // countdown (only after concentrate animation)
    } else if (isShowingTime) {
        currentRenderMode = 4;  // timeDisplay
    }

    // Check for mode change and execute full screen clear if needed
    compositor.updateRenderMode(currentRenderMode);

    // Local aliases for rendering code
    uint16_t* eyeBuffer = compositor.getBuffer();
    const EyePosition& leftEyePos = compositor.getLeftEyePos();
    const EyePosition& rightEyePos = compositor.getRightEyePos();

    // First-boot WiFi info screen
    if (networkSetup.isShowingSetup()) {
        if (networkSetup.checkAPClientConnected()) {
            compositor.requestFullScreenClear();
        }

        // Render first-boot WiFi info screen
        settingsMenu.renderFirstBootSetup(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                                          renderer.getColor());
        compositor.fullBlit();
        return;
    }

    // WiFi choice screen (Configure WiFi / Use Offline)
    if (networkSetup.isShowingChoice()) {
        // touchX is effective Y due to 90° CCW rotation
        if (networkSetup.handleChoiceTouch(isTouching, touchX, COMBINED_BUF_HEIGHT)) {
            compositor.requestFullScreenClear();
        }

        // Render WiFi choice screen
        settingsMenu.renderWiFiChoiceScreen(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                                            renderer.getColor());
        compositor.fullBlit();
        return;
    }

    // If reminder is showing, render it
    if (reminderManager.isShowing()) {
        reminderManager.renderPrompt(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                                      renderer.getColor());
        compositor.fullBlit();
        compositor.prevFrameWasMenu = true;
        compositor.invalidatePrevRects();
        return;
    }

    // If breathing exercise prompt is showing, render it
    if (breathingExercise.isShowingPrompt()) {
        breathingExercise.renderPromptScreen(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                                              renderer.getColor());
        compositor.fullBlit();
        return;
    }

    // If breathing confirmation screen is showing ("Let's Breathe")
    if (breathingExercise.isInConfirmation()) {
        breathingExercise.renderConfirmationScreen(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                                                    renderer.getColor());
        compositor.fullBlit();
        return;
    }

    // If sleeping, render breathing bars and skip normal eye rendering
    if (sleepBehavior.isSleeping()) {
        compositor.renderSleepBars(sleepBehavior.getBreathingBrightness());
        return;
    }

    // During active pomodoro (except celebration), show countdown timer instead of eyes
    // BUT: if concentrate animation is playing, show eyes first
    if (pomodoroTimer.isActive() &&
        pomodoroState != PomodoroState::Celebration &&
        pomodoroState != PomodoroState::WaitingForTap &&
        !timerVisuals.isConcentrating()) {
        uint32_t remainingSec = pomodoroTimer.getRemainingSeconds();
        int minutes = remainingSec / 60;
        int seconds = remainingSec % 60;

        // Blink colon at 500ms intervals
        bool showColon = ((now / 500) % 2) == 0;

        // Determine label based on pomodoro state
        const char* stateLabel = nullptr;
        switch (pomodoroState) {
            case PomodoroState::Working:
                stateLabel = "WORK";
                break;
            case PomodoroState::ShortBreak:
                stateLabel = "BREAK";
                break;
            case PomodoroState::LongBreak:
                stateLabel = "LONG BREAK";
                break;
            default:
                break;
        }

        // Render countdown timer to buffer with label
        settingsMenu.renderCountdown(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                                      minutes, seconds, renderer.getColor(), showColon, stateLabel);

        gfx->startWrite();
        compositor.renderPomodoroProgressBar(pomodoroTimer.getProgress(), false, true);  // Progressive corners
        // Blit only the safe central region that doesn't overlap corners
        compositor.blitSafeCenter(false);  // false = don't manage write
        gfx->endWrite();

        // Mark that we need full blit when exiting this mode
        compositor.prevFrameWasMenu = true;
        compositor.invalidatePrevRects();
        return;
    }

    // During active countdown timer, show countdown instead of eyes
    if (countdownTimer.isActive() && countdownState == CountdownState::Running) {
        uint32_t remainingSec = countdownTimer.getRemainingSeconds();
        int minutes = remainingSec / 60;
        int seconds = remainingSec % 60;
        bool showColon = ((now / 500) % 2) == 0;

        settingsMenu.renderCountdown(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                                      minutes, seconds, renderer.getColor(), showColon,
                                      countdownTimer.getTimerName());

        gfx->startWrite();
        compositor.renderPomodoroProgressBar(countdownTimer.getProgress(), false, true);
        compositor.blitSafeCenter(false);
        gfx->endWrite();

        compositor.prevFrameWasMenu = true;
        compositor.invalidatePrevRects();
        return;
    }

    // During expired timer, show pulsing 00:00 with progress frame
    if (countdownTimer.isActive() && countdownState == CountdownState::Expired) {
        float pulse = countdownTimer.getExpiredPulse();
        uint16_t eyeColor = renderer.getColor();
        // Scale RGB565 color by pulse brightness
        uint16_t r = (uint16_t)(((eyeColor >> 11) & 0x1F) * pulse);
        uint16_t g = (uint16_t)(((eyeColor >> 5) & 0x3F) * pulse);
        uint16_t b = (uint16_t)((eyeColor & 0x1F) * pulse);
        uint16_t pulsedColor = (r << 11) | (g << 5) | b;

        settingsMenu.renderCountdown(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                                      0, 0, pulsedColor, true,
                                      countdownTimer.getTimerName());

        // Pulse the progress frame too
        renderer.setColor(pulsedColor);
        gfx->startWrite();
        compositor.renderPomodoroProgressBar(1.0f, false, false);  // Full frame, pulsed color
        compositor.blitSafeCenter(false);
        gfx->endWrite();
        renderer.setColor(eyeColor);  // Restore original color

        compositor.prevFrameWasMenu = true;
        compositor.invalidatePrevRects();
        return;
    }

    // Periodic time display - show for 3 seconds every minute
    static bool needFullBlitAfterTime = false;
    if (isShowingTime) {
        if (now - timeDisplayStart < TIME_DISPLAY_DURATION) {
            // Blink colon at 500ms intervals
            uint32_t elapsed = now - timeDisplayStart;
            bool showColon = ((elapsed / 500) % 2) == 0;

            // Render time overlay using eye color
            settingsMenu.renderTimeOnly(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                                        renderer.getColor(), showColon);
            compositor.fullBlit();
            needFullBlitAfterTime = true;
            return;
        } else {
            // Time display finished
            isShowingTime = false;
        }
    }

    // Note: needFullBlitAfterTime is handled in the rendering section below
    // to ensure full screen blit clears time display artifacts

    // Update blink animation (disabled during breathing exercise for focus)
    if (!breathingExercise.isActive()) {
        blinkController.update(deltaTime, idle.shouldBlink(), idle.isDoubleBlink(), idle.getBlinkSpeed());
    }

    // Update eye animator (gaze, shape building, tweening)
    eyeAnimator.update(deltaTime, isTouching, touchX, touchY, lastTouchTime,
                       isPetted, pettingPulsePhase, moodModifiers);

    // Get final interpolated shapes for rendering
    const EyeShape& leftEye = eyeAnimator.getLeftEye();
    const EyeShape& rightEye = eyeAnimator.getRightEye();

    // Render to combined buffer
    if (settingsMenu.isOpen()) {
        // Full-screen settings menu needs full clear
        compositor.clearBuffer();
        settingsMenu.render(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                            leftEyePos.bufX, leftEyePos.bufY, audio.getLevel());

        // Apply settings in real-time while menu is open
        audioPlayer.setVolume(settingsMenu.getVolume());
        audioPlayer.setMicGain(settingsMenu.getMicSensitivity());
        audio.setThreshold(settingsMenu.getMicThreshold() / 100.0f);
        renderer.setColor(settingsMenu.getColorRGB565());
        compositor.prevFrameWasMenu = true;
        compositor.invalidatePrevRects();
    } else {
        // Prepare buffer: dirty rect clearing or full clear
        bool needFullBlit = compositor.prepareEyeFrame(compositor.prevFrameWasMenu || needFullBlitAfterTime);
        needFullBlitAfterTime = false;

        // Check if we're in active breathing phase (Inhale/HoldIn/Exhale/HoldOut)
        // During these phases, show large centered text instead of eyes
        BreathingState breathState = breathingExercise.getState();
        bool inBreathingPhase = (breathState == BreathingState::Inhale ||
                                 breathState == BreathingState::HoldIn ||
                                 breathState == BreathingState::Exhale ||
                                 breathState == BreathingState::HoldOut);

        // Calculate eye positions (needed for dirty rects even during breathing)
        int16_t bounceOffset = 0;
        bool shouldBounce = reactiveAnims.isJoy() || isPetted ||
            (expressionManager.getCurrent() == Expression::Content && (pomodoroTimer.isActive() || reactiveAnims.isBreathingContent())) ||
            (expressionManager.getCurrent() == Expression::Relaxed && reactiveAnims.isBreathingRelaxed());
        if (shouldBounce) {
            // Bounce up and down (sin oscillates -1 to +1), 15px amplitude each direction
            bounceOffset = (int16_t)(sinf(reactiveAnims.getBouncePhase() * 2.0f * PI) * 15.0f);
        }
        int16_t leftCX = leftEyePos.baseX - bounceOffset;
        int16_t rightCX = rightEyePos.baseX - bounceOffset;

        // Compute eye bounding boxes (before render, for dirty tracking)
        DirtyRect curLeftRect = compositor.computeEyeRect(leftEye, leftCX, leftEyePos.baseY);
        DirtyRect curRightRect = compositor.computeEyeRect(rightEye, rightCX, rightEyePos.baseY);

        if (inBreathingPhase) {
            if (breathingExercise.getExerciseType() == BreathingType::NadiShodhana) {
                // Nadi Shodhana: render eyes (asymmetric wink) with text overlay below
                memset(eyeBuffer, 0, COMBINED_BUF_WIDTH * COMBINED_BUF_HEIGHT * sizeof(uint16_t));
                renderer.renderToBuf(leftEye, eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                                     leftCX, leftEyePos.baseY, true, false);
                renderer.renderToBuf(rightEye, eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                                     rightCX, rightEyePos.baseY, false, false);
                breathingExercise.renderPhaseTextOverlay(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                                                         renderer.getColor());
            } else {
                // Box breathing: full-screen text (replaces eyes)
                breathingExercise.renderPhaseText(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                                                   renderer.getColor());
            }
            // Use safe region blit to avoid overwriting progress bar corners
            // Same approach as Pomodoro countdown
            needFullBlit = false;  // We'll handle blit specially below

            // Invalidate eye rects so next frame after breathing does full clear
            compositor.invalidatePrevRects();

            // Calculate breathing progress bar parameters
            BreathingState breathState = breathingExercise.getState();
            float barProgress = 0.0f;
            float pulseBlend = 0.0f;
            bool reverseDir = false;
            float phaseProgress = breathingExercise.getPhaseProgress();

            switch (breathState) {
                case BreathingState::Inhale:
                    barProgress = phaseProgress;
                    reverseDir = true;
                    pulseBlend = 0.7f * (1.0f - phaseProgress);
                    break;
                case BreathingState::HoldIn:
                    barProgress = 1.0f;
                    pulseBlend = 0.5f + 0.5f * sinf(millis() * 0.002f);
                    break;
                case BreathingState::Exhale:
                    barProgress = 1.0f - phaseProgress;
                    pulseBlend = 0.7f * phaseProgress;
                    break;
                case BreathingState::HoldOut:
                    barProgress = 0.0f;
                    pulseBlend = 0.7f;
                    break;
                default:
                    break;
            }

            gfx->startWrite();
            compositor.renderBreathingProgressBar(barProgress, pulseBlend, reverseDir);
            // Blit only the safe central region that doesn't overlap corners
            compositor.blitSafeCenter(false);
            gfx->endWrite();
            return;  // Skip normal blit path
        } else {
            // Normal eye rendering
            renderer.renderToBuf(leftEye, eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                                 leftCX, leftEyePos.baseY, true, false);
            renderer.renderToBuf(rightEye, eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                                 rightCX, rightEyePos.baseY, false, false);
        }

        // Animate progress bar clearing (when exiting pomodoro/countdown mode)
        float clearProgress = timerVisuals.advanceClearAnimation(now);
        if (clearProgress >= 0.0f) {
            if (clearProgress >= 1.0f) {
                // Animation complete - do final clear to black
                const int16_t screenW = LCD_WIDTH;
                const int16_t screenH = LCD_HEIGHT;
                const int16_t barThick = 16;
                const int16_t cornerR = 42;

                gfx->startWrite();
                gfx->fillRect(0, 0, screenW, barThick, 0);
                gfx->fillRect(0, screenH - barThick, screenW, barThick, 0);
                gfx->fillRect(0, 0, barThick, screenH, 0);
                gfx->fillRect(screenW - barThick, 0, barThick, screenH, 0);
                gfx->fillRect(0, 0, cornerR + 5, cornerR + 5, 0);
                gfx->fillRect(screenW - cornerR - 5, 0, cornerR + 5, cornerR + 5, 0);
                gfx->fillRect(0, screenH - cornerR - 5, cornerR + 5, cornerR + 5, 0);
                gfx->fillRect(screenW - cornerR - 5, screenH - cornerR - 5, cornerR + 5, cornerR + 5, 0);
                gfx->endWrite();

                compositor.lastRenderedFilledLen = -1;
                Serial.println("Progress bar clear complete");
            } else {
                // Animate: deplete the bar from current position to empty
                float animatedProgress = 1.0f - clearProgress;
                compositor.renderPomodoroProgressBar(animatedProgress, true, true);
            }
        }

        compositor.blitEyes(needFullBlit, curLeftRect, curRightRect);

        // Render pomodoro progress bar if active (hide during celebration/waiting/concentrate)
        if (pomodoroTimer.isActive() &&
            pomodoroState != PomodoroState::WaitingForTap &&
            pomodoroState != PomodoroState::Celebration &&
            !timerVisuals.isConcentrating()) {
            compositor.renderPomodoroProgressBar(pomodoroTimer.getProgress(), true, true);  // Progressive corners
        }

        return;
    }

    // Full blit for settings menu or special cases
    compositor.fullBlit();
}
