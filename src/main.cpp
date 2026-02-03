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
#include "animation/tweener.h"
#include "behavior/expressions.h"
#include "behavior/idle_behavior.h"
#include "input/imu_handler.h"
#include "input/audio_handler.h"
#include "behavior/sleep_behavior.h"
#include "behavior/time_mood.h"
#include "audio/audio_player.h"
#include "ui/settings_menu.h"
#include "ui/pomodoro.h"
#include "network/wifi_manager.h"
#include "network/web_server.h"
#include "network/captive_portal.h"

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

// Eye positioning on screen
struct EyePosition {
    int16_t baseX, baseY;   // Base center position
    int16_t bufX, bufY;     // Top-left of buffer region on screen
};

EyePosition leftEyePos, rightEyePos;

// Dirty-rect tracking: previous frame eye bounding boxes
struct DirtyRect {
    int16_t x, y, w, h;
    bool valid;  // false on first frame
};
DirtyRect prevLeftRect = {0, 0, 0, 0, false};
DirtyRect prevRightRect = {0, 0, 0, 0, false};
bool prevFrameWasMenu = false;  // Track if last frame was settings menu

EyeShapeTweener leftEyeTweener, rightEyeTweener;
EyeShape leftEyeBase, rightEyeBase;      // Base expression shape
EyeShape leftEyeTarget, rightEyeTarget;  // Target shapes (with gaze/blink applied)
EyeShape leftEye, rightEye;              // Current (interpolated) shapes
EyeRenderer renderer;
IdleBehavior idle;
ImuHandler imu;
AudioHandler audio;
SleepBehavior sleepBehavior;
AudioPlayer audioPlayer;
SettingsMenu settingsMenu;
PomodoroTimer pomodoroTimer;
WiFiManager wifiManager;
WebServerManager webServer;
CaptivePortal captivePortal;

// Current expression
Expression currentExpression = Expression::Neutral;

// Frame timing
uint32_t lastFrameTime = 0;
float deltaTime = 0.016f;  // 60fps default

// Blink state (now driven by IdleBehavior)
float blinkProgress = 0;
bool isBlinking = false;
int blinkCount = 0;        // For double-blink
float blinkSpeed = 1.0f;

// Touch state
int16_t touchX = -1, touchY = -1;
bool isTouching = false;
uint32_t lastTouchTime = 0;
uint32_t touchStartTime = 0;
bool wasTouching = false;
bool isPetted = false;              // Currently being petted
Expression preGestureExpression = Expression::Neutral;  // Expression before gesture

// Double-tap detection for settings menu
uint32_t lastTapTime = 0;
const uint32_t DOUBLE_TAP_WINDOW = 350;  // Max time between taps for double-tap

// Debug expression tap - auto-reverts after 5 seconds
bool debugExpressionActive = false;
uint32_t debugExpressionStart = 0;
Expression expressionBeforeDebugTap = Expression::Neutral;
const uint32_t DEBUG_EXPRESSION_DURATION = 5000;  // 5 seconds

// IMU reaction state
bool isImuReacting = false;         // Currently showing IMU-triggered expression
uint32_t imuReactionStart = 0;      // When IMU reaction started
const uint32_t IMU_REACTION_DURATION = 4000;  // How long to show IMU reaction (ms)

// Petting brightness pulse
float pettingPulsePhase = 0.0f;
bool pettingSoundPlayed = false;  // Track if we've played the happy sound

// Sleep wake tracking
bool wasSleepingOrDrowsy = false;  // Track previous sleep state for wake detection

// Irritated state (when environment is too loud)
bool showingIrritated = false;
uint32_t irritatedStart = 0;
const uint32_t IRRITATED_DURATION = 3000;       // Show grumpy for 3s
Expression expressionBeforeIrritated = Expression::Neutral;

// Love hearts after petting
bool showingLove = false;
uint32_t loveStart = 0;
const uint32_t LOVE_DURATION = 4000;            // Show hearts for 4s after petting
Expression expressionBeforeLove = Expression::Neutral;

// Joy behavior - random happy moments every 10-30 minutes
bool showingJoy = false;
uint32_t joyStart = 0;
uint32_t nextJoyTime = 0;                       // When to trigger next joy
const uint32_t JOY_DURATION = 3000;             // Show joy for 3s
const uint32_t JOY_MIN_INTERVAL = 10 * 60 * 1000;  // 10 minutes
const uint32_t JOY_MAX_INTERVAL = 30 * 60 * 1000;  // 30 minutes
Expression expressionBeforeJoy = Expression::Neutral;
float joyBouncePhase = 0.0f;                    // For bounce animation

// Animation phase for rotating stars/shapes
float shapeAnimPhase = 0.0f;

// Orientation-based expressions (face-down, tilted long)
bool showingOrientationExpr = false;
Orientation lastOrientation = Orientation::Normal;
Expression expressionBeforeOrientation = Expression::Neutral;

// Time-of-day mood system
TimeMood currentMood = TimeMood::Afternoon;
MoodModifiers moodModifiers = {1.0f, 1.0f, 0.0f, "Afternoon"};

// Pomodoro timer state
bool pomodoroExpressActive = false;
Expression expressionBeforePomodoro = Expression::Neutral;
PomodoroState lastPomodoroState = PomodoroState::Idle;
uint32_t lastPomodoroTick = 0;  // For tick sound timing
int lastRenderedFilledLen = -1;  // Progress bar cache (prevents flicker)
bool needClearProgressBar = false;  // Clear progress bar edges when exiting pomodoro
bool progressBarClearing = false;   // Animating progress bar clear
uint32_t clearAnimStart = 0;        // When clear animation started
float clearAnimProgress = 0.0f;     // 0.0 to 1.0 during clear animation
const uint32_t CLEAR_ANIM_DURATION = 500;  // 500ms to clear

// Render mode tracking for full-screen clears on transitions
// Modes: 0=eyes, 1=menu, 2=countdown, 3=sleep, 4=timeDisplay
int lastRenderMode = 0;
bool needFullScreenClear = false;  // Clear entire physical screen on mode change

// Concentrate animation state (plays when starting work)
// Phase: 0=none, 1=eyes closed, 2=eyes wide (Alert), 3=settling to Focused
int concentratePhase = 0;
uint32_t concentrateStart = 0;
const uint32_t CONCENTRATE_CLOSE_DURATION = 600;   // Eyes closed for 0.6s
const uint32_t CONCENTRATE_ALERT_DURATION = 900;   // Eyes wide for 0.9s (total 1.5s)

// Periodic time display
uint32_t lastTimeTick = 0;              // When we last advanced the clock
bool isShowingTime = false;             // Currently showing time overlay
uint32_t timeDisplayStart = 0;          // When the current time display started
const uint32_t TIME_DISPLAY_DURATION = 3000;  // Show time for 3 seconds
const uint32_t TIME_TICK_INTERVAL = 60000;    // Advance clock every 60 seconds

// First-boot WiFi setup screen state
bool isShowingWiFiSetup = false;        // True during first-boot setup screen
bool wifiSetupTouchWasActive = false;   // For detecting button taps
bool wifiWasEnabled = true;             // Track WiFi enabled state for changes
bool wifiWasConnected = false;          // Track WiFi connected state for NTP sync
int8_t lastGmtOffsetHours = 0;          // Track timezone for NTP re-sync

//=============================================================================
// Micro-Expression Behavior - Random idle personality moments
//=============================================================================

enum class MicroExpressionType {
    None,
    // Expression-based (uses Expression enum)
    CuriousGlance,      // Brief asymmetric widening
    ThinkingMoment,     // Look up, pondering
    ContentSmile,       // Brief happy squint
    MischievousLook,    // Sly narrowed eyes
    BoredGlance,        // Heavy lids, look aside
    AlertPerk,          // Sudden widening
    SadMoment,          // Brief melancholy
    SurprisedLook,      // Sudden wide eyes
    AngryFlash,         // Brief angry narrowing
    GrumpyMood,         // Grumpy furrowed look
    FocusedStare,       // Intense concentration
    ConfusedGlance,     // Puzzled asymmetric look
    SmugGrin,           // Self-satisfied narrowing
    DreamyGaze,         // Soft unfocused look
    SkepticalLook,      // One-brow-raised doubt
    SquintPeer,         // Squinting to see something
    // Animation-based (custom gaze/blink patterns)
    Wink,               // One eye blinks
    EyeRoll,            // Look up then circle
    DoubleTake,         // Fast look to side, snap back
    ShiftyEyes,         // Quick back-and-forth
    QuickSigh,          // Close then open wide
};

bool microExprActive = false;
MicroExpressionType currentMicroExpr = MicroExpressionType::None;
uint32_t microExprStart = 0;
uint32_t nextMicroExprTime = 0;
float microExprPhase = 0.0f;              // Animation progress 0-1
Expression expressionBeforeMicro = Expression::Neutral;

// Timing constants
const uint32_t MICRO_EXPR_MIN_INTERVAL = 2 * 60 * 1000;   // 2 minutes
const uint32_t MICRO_EXPR_MAX_INTERVAL = 3 * 60 * 1000;   // 3 minutes
const uint32_t MICRO_EXPR_DURATION_SHORT = 800;           // 0.8s for quick ones
const uint32_t MICRO_EXPR_DURATION_MEDIUM = 1500;         // 1.5s for medium
const uint32_t MICRO_EXPR_DURATION_LONG = 2500;           // 2.5s for eye roll/shifty
const uint32_t MICRO_EXPR_DURATION_MOOD_MIN = 60 * 1000;  // 1 minute minimum for moods
const uint32_t MICRO_EXPR_DURATION_MOOD_MAX = 180 * 1000; // 3 minutes maximum for moods

// Wink state
bool winkLeftEye = true;                  // Which eye to wink
float winkProgress = 0.0f;

// Expression timeout - return to Neutral after 5s if not controlled by active behavior
uint32_t lastExpressionChange = 0;
const uint32_t EXPRESSION_TIMEOUT = 5000;  // 5 seconds

// Gaze tracking with tweeners
Tweener gazeX, gazeY;

// Combined framebuffer for both eyes (allocated in PSRAM)
uint16_t *eyeBuffer = nullptr;

// Eye spacing in buffer X (which is screen Y / vertical)
// Eyes are 120px apart center-to-center on screen
#define EYE_SPACING 120

// Display driver
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_CS, LCD_SCLK, LCD_SDIO0, LCD_SDIO1, LCD_SDIO2, LCD_SDIO3
);

Arduino_SH8601 *gfx = new Arduino_SH8601(
    bus, -1, 0, LCD_WIDTH, LCD_HEIGHT
);

void setExpression(Expression expr) {
    currentExpression = expr;
    leftEyeBase = getExpressionShape(expr, true);
    rightEyeBase = getExpressionShape(expr, false);
    lastExpressionChange = millis();  // Track when expression changed

    // Variable transition timing based on expression type
    float smoothTime = 0.2f;  // Default
    switch (expr) {
        // Fast snap for sudden reactions
        case Expression::Startled:
        case Expression::Scared:
        case Expression::Alert:
            smoothTime = 0.08f;
            break;

        // Quick but not instant for surprise/joy
        case Expression::Surprised:
        case Expression::Joy:
        case Expression::Joyful:
        case Expression::Wink:
            smoothTime = 0.12f;
            break;

        // Slow, heavy transitions for tired/sad emotions
        case Expression::Sad:
        case Expression::Sleepy:
        case Expression::Bored:
        case Expression::Yawn:
            smoothTime = 0.35f;
            break;

        // Medium-slow for relaxed states
        case Expression::Content:
        case Expression::ContentPetting:
        case Expression::Dreamy:
        case Expression::Love:
            smoothTime = 0.25f;
            break;

        // Default timing for other expressions
        default:
            smoothTime = 0.2f;
            break;
    }
    leftEyeTweener.setSmoothTime(smoothTime);
    rightEyeTweener.setSmoothTime(smoothTime);

    Serial.printf("Expression: %s (%.2fs)\n", getExpressionName(expr), smoothTime);
}

void nextExpression() {
    int next = ((int)currentExpression + 1) % (int)Expression::COUNT;
    setExpression((Expression)next);
}

// Web expression preview callback
void onWebExpressionPreview(int index) {
    if (index >= 0 && index < (int)Expression::COUNT) {
        setExpression((Expression)index);
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
    return getExpressionName(currentExpression);
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
                    // Single tap - cycle through expressions (debug mode, auto-reverts)
                    lastTapTime = now;
                    if (!debugExpressionActive) {
                        expressionBeforeDebugTap = currentExpression;
                    }
                    nextExpression();
                    debugExpressionActive = true;
                    debugExpressionStart = now;
                    preGestureExpression = currentExpression;
                    Serial.println("Debug expression - will revert in 5s");
                }
            }

            // Transition to Love hearts after petting ends
            if (isPetted) {
                expressionBeforeLove = preGestureExpression;
                setExpression(Expression::Love);
                showingLove = true;
                loveStart = now;
                isPetted = false;
                Serial.println("Petting ended - showing hearts");
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
        preGestureExpression = currentExpression;
    } else {
        // Ongoing touch - check for petting
        uint32_t duration = now - touchStartTime;

        if (!isPetted && duration >= PET_MIN_DURATION) {
            // Transition to content petting expression (half-moon arches)
            isPetted = true;
            debugExpressionActive = false;  // Cancel debug mode
            showingJoy = false;  // Cancel joy mode
            // Cancel any active micro-expression/mood - petting resets to happy idle
            if (microExprActive) {
                microExprActive = false;
                currentMicroExpr = MicroExpressionType::None;
                Serial.println("Mood cancelled by petting");
            }
            preGestureExpression = Expression::Neutral;  // Return to neutral after petting
            pettingPulsePhase = 0.0f;
            joyBouncePhase = 0.0f;  // Reset bounce for anime slit animation
            pettingSoundPlayed = false;  // Reset so we can play
            setExpression(Expression::ContentPetting);
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

void initEyePositions() {
    int16_t centerX = SCREEN_WIDTH / 2;
    int16_t centerY = SCREEN_HEIGHT / 2;

    // Using a single combined buffer for both eyes
    // 90° CCW rotation: Buffer X → screen vertical, Buffer Y → screen horizontal
    // Eyes side-by-side HORIZONTALLY on screen = different buffer Y positions

    // Position buffer inside 16px progress bar margins
    // Buffer starts at (16, 16) to avoid overlapping the progress bar edges
    leftEyePos.bufX = 16;  // Leave 16px margin for progress bar
    leftEyePos.bufY = 16;  // Leave 16px margin for progress bar

    // Eye center positions WITHIN the combined buffer
    // Buffer 336x416, positioned at (16,16) inside progress bar margins
    leftEyePos.baseX = COMBINED_BUF_WIDTH / 2;                       // 168 (vertical center on screen)
    leftEyePos.baseY = COMBINED_BUF_HEIGHT / 2 - EYE_SPACING / 2;    // 208 - 60 = 148 (left on screen)

    rightEyePos.baseX = COMBINED_BUF_WIDTH / 2;                      // 168 (same vertical center)
    rightEyePos.baseY = COMBINED_BUF_HEIGHT / 2 + EYE_SPACING / 2;   // 208 + 60 = 268 (right on screen)

    // Store screen position for blitting (same for both since single buffer)
    rightEyePos.bufX = leftEyePos.bufX;
    rightEyePos.bufY = leftEyePos.bufY;

    Serial.printf("Combined buffer: %dx%d at screen (%d,%d)\n",
                  COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                  leftEyePos.bufX, leftEyePos.bufY);
    Serial.printf("Eye centers in buffer: L(%d,%d) R(%d,%d)\n",
                  leftEyePos.baseX, leftEyePos.baseY,
                  rightEyePos.baseX, rightEyePos.baseY);
}

void triggerBlink() {
    if (!isBlinking) {
        isBlinking = true;
        blinkProgress = 0;
        blinkCount = idle.isDoubleBlink() ? 2 : 1;
        blinkSpeed = idle.getBlinkSpeed();
    }
}

void updateBlink() {
    // Check if idle behavior wants to trigger a blink
    if (idle.shouldBlink()) {
        triggerBlink();
    }

    if (isBlinking) {
        blinkProgress += 0.48f * blinkSpeed;  // Doubled blink speed
        if (blinkProgress >= 2.0f) {
            blinkCount--;
            if (blinkCount > 0) {
                // More blinks to do (double-blink)
                blinkProgress = 0;
            } else {
                // Done blinking
                isBlinking = false;
                blinkProgress = 0;
            }
        }
    }
}

float getBlinkOpenness() {
    if (!isBlinking) return 1.0f;

    // Hold fully closed when near midpoint (ensures full closure with fast animation)
    if (blinkProgress >= 0.85f && blinkProgress <= 1.15f) {
        return 0.0f;  // Fully closed
    }

    if (blinkProgress < 0.85f) {
        // Closing phase: scale 0-0.85 to openness 1.0-0.0
        return 1.0f - (blinkProgress / 0.85f);
    } else {
        // Opening phase: scale 1.15-2.0 to openness 0.0-1.0
        return (blinkProgress - 1.15f) / 0.85f;
    }
}

void updateGaze() {
    if (isTouching) {
        // Convert touch position to normalized gaze (-1 to 1)
        float targetX = (touchX - SCREEN_WIDTH / 2) / (float)(SCREEN_WIDTH / 2);
        float targetY = (touchY - SCREEN_HEIGHT / 2) / (float)(SCREEN_HEIGHT / 2);
        // Clamp to valid range
        gazeX.setTarget(constrain(targetX, -1.0f, 1.0f));
        gazeY.setTarget(constrain(targetY, -1.0f, 1.0f));
    } else if (millis() - lastTouchTime > 500) {
        // When not touching, use idle gaze
        gazeX.setTarget(idle.getIdleGazeX());
        gazeY.setTarget(idle.getIdleGazeY());
    }

    // Update tweeners
    gazeX.update(deltaTime);
    gazeY.update(deltaTime);
}
//=============================================================================
// Micro-Expression Functions
//=============================================================================

void triggerRandomMicroExpression() {
    // Pick a random micro-expression type
    // Weight towards simpler ones, rare for complex animations
    int r = random(200);

    if (r < 14) {
        currentMicroExpr = MicroExpressionType::CuriousGlance;
    } else if (r < 26) {
        currentMicroExpr = MicroExpressionType::ThinkingMoment;
    } else if (r < 38) {
        currentMicroExpr = MicroExpressionType::ContentSmile;
    } else if (r < 48) {
        currentMicroExpr = MicroExpressionType::MischievousLook;
    } else if (r < 58) {
        currentMicroExpr = MicroExpressionType::BoredGlance;
    } else if (r < 68) {
        currentMicroExpr = MicroExpressionType::AlertPerk;
    } else if (r < 76) {
        currentMicroExpr = MicroExpressionType::SadMoment;
    } else if (r < 84) {
        currentMicroExpr = MicroExpressionType::SurprisedLook;
    } else if (r < 91) {
        currentMicroExpr = MicroExpressionType::AngryFlash;
    } else if (r < 98) {
        currentMicroExpr = MicroExpressionType::GrumpyMood;
    } else if (r < 106) {
        currentMicroExpr = MicroExpressionType::FocusedStare;
    } else if (r < 114) {
        currentMicroExpr = MicroExpressionType::ConfusedGlance;
    } else if (r < 122) {
        currentMicroExpr = MicroExpressionType::SmugGrin;
    } else if (r < 130) {
        currentMicroExpr = MicroExpressionType::DreamyGaze;
    } else if (r < 138) {
        currentMicroExpr = MicroExpressionType::SkepticalLook;
    } else if (r < 146) {
        currentMicroExpr = MicroExpressionType::SquintPeer;
    } else if (r < 156) {
        currentMicroExpr = MicroExpressionType::Wink;
        winkLeftEye = random(2) == 0;  // Random eye
    } else if (r < 166) {
        currentMicroExpr = MicroExpressionType::DoubleTake;
    } else if (r < 178) {
        currentMicroExpr = MicroExpressionType::ShiftyEyes;
    } else if (r < 192) {
        currentMicroExpr = MicroExpressionType::QuickSigh;
    } else {
        currentMicroExpr = MicroExpressionType::EyeRoll;  // Rarest
    }

    expressionBeforeMicro = currentExpression;
    microExprActive = true;
    microExprStart = millis();
    microExprPhase = 0.0f;
    winkProgress = 0.0f;

    // Set expression for expression-based micro-expressions
    switch (currentMicroExpr) {
        case MicroExpressionType::CuriousGlance:
            setExpression(Expression::Curious);
            break;
        case MicroExpressionType::ThinkingMoment:
            setExpression(Expression::Thinking);
            break;
        case MicroExpressionType::ContentSmile:
            setExpression(Expression::Happy);
            break;
        case MicroExpressionType::MischievousLook:
            setExpression(Expression::Mischievous);
            break;
        case MicroExpressionType::BoredGlance:
            setExpression(Expression::Bored);
            break;
        case MicroExpressionType::AlertPerk:
            setExpression(Expression::Alert);
            break;
        case MicroExpressionType::SadMoment:
            setExpression(Expression::Sad);
            break;
        case MicroExpressionType::SurprisedLook:
            setExpression(Expression::Surprised);
            break;
        case MicroExpressionType::AngryFlash:
            setExpression(Expression::Angry);
            break;
        case MicroExpressionType::GrumpyMood:
            setExpression(Expression::Grumpy);
            break;
        case MicroExpressionType::FocusedStare:
            setExpression(Expression::Focused);
            break;
        case MicroExpressionType::ConfusedGlance:
            setExpression(Expression::Confused);
            break;
        case MicroExpressionType::SmugGrin:
            setExpression(Expression::Smug);
            break;
        case MicroExpressionType::DreamyGaze:
            setExpression(Expression::Dreamy);
            break;
        case MicroExpressionType::SkepticalLook:
            setExpression(Expression::Skeptical);
            break;
        case MicroExpressionType::SquintPeer:
            setExpression(Expression::Squint);
            break;
        default:
            // Animation-based ones don't change expression immediately
            break;
    }

    Serial.printf("Micro-expression: %d\n", (int)currentMicroExpr);
}

uint32_t getMicroExprDuration() {
    switch (currentMicroExpr) {
        case MicroExpressionType::CuriousGlance:
        case MicroExpressionType::AlertPerk:
        case MicroExpressionType::QuickSigh:
        case MicroExpressionType::SurprisedLook:     // Quick flash of surprise
        case MicroExpressionType::AngryFlash:         // Brief irritation
            return MICRO_EXPR_DURATION_SHORT;
        case MicroExpressionType::ThinkingMoment:
        case MicroExpressionType::ContentSmile:
        case MicroExpressionType::MischievousLook:
        case MicroExpressionType::BoredGlance:
        case MicroExpressionType::Wink:
        case MicroExpressionType::DoubleTake:
        case MicroExpressionType::ConfusedGlance:     // Puzzled pause
        case MicroExpressionType::SmugGrin:            // Held for effect
        case MicroExpressionType::SkepticalLook:      // Deliberate doubt
        case MicroExpressionType::SquintPeer:          // Peering at something
            return MICRO_EXPR_DURATION_MEDIUM;
        case MicroExpressionType::EyeRoll:
        case MicroExpressionType::ShiftyEyes:
            return MICRO_EXPR_DURATION_LONG;
        case MicroExpressionType::GrumpyMood:         // Lingering mood (1-3 min)
        case MicroExpressionType::FocusedStare:
        case MicroExpressionType::DreamyGaze:
        case MicroExpressionType::SadMoment:
            return MICRO_EXPR_DURATION_MOOD_MIN + random(MICRO_EXPR_DURATION_MOOD_MAX - MICRO_EXPR_DURATION_MOOD_MIN);
        default:
            return MICRO_EXPR_DURATION_SHORT;
    }
}

// Returns gaze offset for animation-based micro-expressions
void getMicroExprGazeOffset(float& offsetX, float& offsetY) {
    offsetX = 0;
    offsetY = 0;

    switch (currentMicroExpr) {
        case MicroExpressionType::DoubleTake:
            // Fast look to side (first half), snap back (second half)
            if (microExprPhase < 0.3f) {
                offsetY = microExprPhase / 0.3f * 0.5f;  // Look right
            } else if (microExprPhase < 0.5f) {
                offsetY = 0.5f;  // Hold
            } else {
                offsetY = 0.5f * (1.0f - (microExprPhase - 0.5f) / 0.5f);  // Snap back
            }
            break;

        case MicroExpressionType::ShiftyEyes:
            // Quick back-and-forth
            {
                float cycle = fmodf(microExprPhase * 4.0f, 1.0f);  // 4 cycles
                offsetY = sinf(cycle * 2.0f * M_PI) * 0.4f;
            }
            break;

        case MicroExpressionType::EyeRoll:
            // Circular motion: up, right, down, left
            {
                float angle = microExprPhase * 2.0f * M_PI;
                offsetX = -sinf(angle) * 0.35f;  // Vertical (up first)
                offsetY = cosf(angle) * 0.35f;   // Horizontal
            }
            break;

        default:
            break;
    }
}

// Returns openness modifier for wink/sigh animations
float getMicroExprOpenness(bool isLeftEye) {
    switch (currentMicroExpr) {
        case MicroExpressionType::Wink:
            // Only affects one eye
            if ((isLeftEye && winkLeftEye) || (!isLeftEye && !winkLeftEye)) {
                // This eye winks
                if (microExprPhase < 0.3f) {
                    return 1.0f - (microExprPhase / 0.3f);  // Close
                } else if (microExprPhase < 0.6f) {
                    return 0.0f;  // Hold closed
                } else {
                    return (microExprPhase - 0.6f) / 0.4f;  // Open
                }
            }
            return 1.0f;

        case MicroExpressionType::QuickSigh:
            // Both eyes close then open wider
            if (microExprPhase < 0.25f) {
                return 1.0f - (microExprPhase / 0.25f) * 0.7f;  // Close to 30%
            } else if (microExprPhase < 0.5f) {
                return 0.3f;  // Hold
            } else {
                float openPhase = (microExprPhase - 0.5f) / 0.5f;
                return 0.3f + openPhase * 0.8f;  // Open to 110%
            }

        default:
            return 1.0f;
    }
}

// Clear a rectangular region of the buffer to black (0x0000)
void clearRect(uint16_t* buffer, int16_t bufW, int16_t bufH,
               int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
    // Clamp to buffer bounds
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > bufW) rw = bufW - rx;
    if (ry + rh > bufH) rh = bufH - ry;
    if (rw <= 0 || rh <= 0) return;

    // Clear row by row using memset (BG_COLOR is 0x0000)
    for (int16_t y = ry; y < ry + rh; y++) {
        memset(&buffer[y * bufW + rx], 0, rw * sizeof(uint16_t));
    }
}

// Compute eye bounding box from shape and center position
// Accounts for different shape types (star, heart, swirl, circle, rectangle)
DirtyRect computeEyeRect(const EyeShape& shape, int16_t centerX, int16_t centerY, int16_t margin = 10) {
    int16_t ox = shape.getOffsetXPixels();
    int16_t oy = shape.getOffsetYPixels();
    int16_t eyeHeight = shape.getHeight();
    int16_t w, h;

    switch (shape.shapeType) {
        case ShapeType::Star: {
            // Star: outerR = eyeHeight * 0.6f, extends ±outerR from center
            int16_t outerR = (int16_t)(eyeHeight * 0.6f);
            w = h = outerR * 2;
            break;
        }
        case ShapeType::Heart: {
            // Heart: size = eyeHeight * 0.5f, extends roughly 1.5x in each direction
            int16_t heartSize = (int16_t)(eyeHeight * 0.5f);
            w = h = (int16_t)(heartSize * 3);  // Conservative estimate
            break;
        }
        case ShapeType::Swirl: {
            // Swirl: size = eyeHeight * 0.6f, extends ±size from center
            int16_t swirlSize = (int16_t)(eyeHeight * 0.6f);
            w = h = swirlSize * 2;
            break;
        }
        case ShapeType::Circle: {
            // Circle: radius = eyeHeight * 0.5f
            int16_t circleR = (int16_t)(eyeHeight * 0.5f);
            w = h = circleR * 2;
            break;
        }
        default:  // Rectangle
            w = shape.getWidth();
            h = shape.getHeight();
            break;
    }

    DirtyRect r;
    r.x = centerX - w / 2 + ox - margin;
    r.y = centerY - h / 2 + oy - margin;
    r.w = w + margin * 2;
    r.h = h + margin * 2;
    r.valid = true;
    return r;
}

// Compute union of two dirty rects (bounding box containing both)
DirtyRect unionRect(const DirtyRect& a, const DirtyRect& b) {
    if (!a.valid) return b;
    if (!b.valid) return a;
    DirtyRect u;
    int ax2 = a.x + a.w, ay2 = a.y + a.h;
    int bx2 = b.x + b.w, by2 = b.y + b.h;
    u.x = (a.x < b.x) ? a.x : b.x;
    u.y = (a.y < b.y) ? a.y : b.y;
    u.w = ((ax2 > bx2) ? ax2 : bx2) - u.x;
    u.h = ((ay2 > by2) ? ay2 : by2) - u.y;
    u.valid = true;
    return u;
}

// Blit a sub-region of the buffer to the display
// srcBuffer: source pixel buffer with stride bufW
// bufX, bufY: top-left position of buffer on screen
// region: sub-region within the buffer to blit (in buffer coords)
// manageWrite: if true, calls startWrite/endWrite; if false, caller must manage
void blitRegion(uint16_t* srcBuffer, int16_t bufW, int16_t bufH,
                int16_t bufX, int16_t bufY, const DirtyRect& region,
                bool manageWrite = true) {
    if (!region.valid) return;

    // Clamp region to buffer bounds
    int16_t rx = (region.x > 0) ? region.x : 0;
    int16_t ry = (region.y > 0) ? region.y : 0;
    int rw_end = region.x + region.w;
    int rh_end = region.y + region.h;
    int16_t rw = ((rw_end < bufW) ? rw_end : bufW) - rx;
    int16_t rh = ((rh_end < bufH) ? rh_end : bufH) - ry;
    if (rw <= 0 || rh <= 0) return;

    // Screen destination
    int16_t screenX = bufX + rx;
    int16_t screenY = bufY + ry;

    // Use GFX writeAddrWindow + writePixels for efficient row-by-row blit
    if (manageWrite) gfx->startWrite();
    gfx->writeAddrWindow(screenX, screenY, rw, rh);
    for (int16_t y = 0; y < rh; y++) {
        gfx->writePixels(&srcBuffer[(ry + y) * bufW + rx], rw);
    }
    if (manageWrite) gfx->endWrite();
}

/**
 * Render pomodoro progress bar frame around screen edge
 * Progress depletes clockwise starting from screen top-middle
 * Has rounded corners to match the screen's rounded edges
 *
 * Actual rotation mapping (90° CW effective):
 *   - Screen top = GFX left edge
 *   - Screen right = GFX top edge
 *   - Screen bottom = GFX right edge
 *   - Screen left = GFX bottom edge
 */
void renderPomodoroProgressBar(float progress, bool manageWrite = true, bool progressiveCorners = false) {
    // Screen dimensions in GFX coordinates (no rotation applied to GFX)
    const int16_t screenW = LCD_WIDTH;   // 368
    const int16_t screenH = LCD_HEIGHT;  // 448
    const int16_t barThick = 16;         // Bar thickness (2x original)
    const int16_t cornerR = 42;          // Corner radius

    // Colors
    uint16_t fillColor = renderer.getColor();  // Eye color for filled
    uint16_t emptyColor = 0x2104;              // Dark gray for empty

    // Calculate lengths for each segment
    int halfLeftLen = (screenH / 2) - cornerR;
    int topLen = screenW - 2 * cornerR;
    int rightLen = screenH - 2 * cornerR;
    int bottomLen = screenW - 2 * cornerR;
    int otherHalfLeftLen = screenH - (screenH / 2) - cornerR;

    // Total perimeter length (simplified - just sum of straight edges + estimated corner length)
    int cornerLen = (int)(1.57f * cornerR);  // π/2 * r per corner
    int totalLen = halfLeftLen + bottomLen + rightLen + topLen + otherHalfLeftLen + 4 * cornerLen;

    // How much is filled (in perimeter pixels)
    int filledLen = (int)(progress * totalLen);

    // Always redraw - the buffer blit overwrites parts of the bar edges
    // so we must redraw every frame to keep it visible

    if (manageWrite) gfx->startWrite();

    int pos = 0;  // Current position along perimeter

    // Helper to determine color at a position
    auto getColor = [&](int p) -> uint16_t {
        return (p < filledLen) ? fillColor : emptyColor;
    };

    // Arc parameters - use slightly smaller radius for arc center path
    float arcCenterR = cornerR - barThick / 2.0f;
    int arcSteps = 8;  // Steps for drawing arc shape
    int arcCircleR = barThick / 2 + 3;  // Larger circles to fill gaps

    // Helper to draw a corner arc
    // When progressiveCorners=false: all circles same color (prevents flicker during normal use)
    // When progressiveCorners=true: each circle colored based on its position (for smooth clearing)
    auto drawCornerArc = [&](float startAngle, float endAngle, int16_t centerX, int16_t centerY,
                              uint16_t color, int cornerStartPos, int cornerLength) {
        for (int i = 0; i < arcSteps; i++) {
            float t = (float)i / (arcSteps - 1);
            float angle = startAngle + (endAngle - startAngle) * t;
            int16_t cx = centerX + (int16_t)(cosf(angle) * arcCenterR);
            int16_t cy = centerY + (int16_t)(sinf(angle) * arcCenterR);

            uint16_t circleColor = color;
            if (progressiveCorners) {
                // Calculate this circle's position in the perimeter
                int circlePos = cornerStartPos + (int)(t * cornerLength);
                circleColor = (circlePos < filledLen) ? fillColor : emptyColor;
            }
            gfx->fillCircle(cx, cy, arcCircleR, circleColor);
        }
    };

    // === Segment 1: GFX left edge, middle going DOWN ===
    // Draw as one rectangle per color region
    {
        int segStart = pos;
        int segEnd = pos + halfLeftLen;

        if (filledLen >= segEnd) {
            // Fully filled
            gfx->fillRect(0, screenH / 2, barThick, halfLeftLen, fillColor);
        } else if (filledLen <= segStart) {
            // Fully empty
            gfx->fillRect(0, screenH / 2, barThick, halfLeftLen, emptyColor);
        } else {
            // Partial - split into two rects
            int fillPx = filledLen - segStart;
            gfx->fillRect(0, screenH / 2, barThick, fillPx, fillColor);
            gfx->fillRect(0, screenH / 2 + fillPx, barThick, halfLeftLen - fillPx, emptyColor);
        }
        pos = segEnd;
    }

    // === Segment 2: GFX bottom-left corner arc ===
    {
        int segStart = pos;
        int segMid = pos + cornerLen / 2;
        uint16_t cornerColor = (filledLen >= segMid) ? fillColor : emptyColor;
        drawCornerArc(M_PI, M_PI / 2, cornerR, screenH - cornerR, cornerColor, segStart, cornerLen);
        pos += cornerLen;
    }

    // === Segment 3: GFX bottom edge, left to right ===
    {
        int segStart = pos;
        int segEnd = pos + bottomLen;

        if (filledLen >= segEnd) {
            gfx->fillRect(cornerR, screenH - barThick, bottomLen, barThick, fillColor);
        } else if (filledLen <= segStart) {
            gfx->fillRect(cornerR, screenH - barThick, bottomLen, barThick, emptyColor);
        } else {
            int fillPx = filledLen - segStart;
            gfx->fillRect(cornerR, screenH - barThick, fillPx, barThick, fillColor);
            gfx->fillRect(cornerR + fillPx, screenH - barThick, bottomLen - fillPx, barThick, emptyColor);
        }
        pos = segEnd;
    }

    // === Segment 4: GFX bottom-right corner arc ===
    {
        int segStart = pos;
        int segMid = pos + cornerLen / 2;
        uint16_t cornerColor = (filledLen >= segMid) ? fillColor : emptyColor;
        drawCornerArc(M_PI / 2, 0, screenW - cornerR, screenH - cornerR, cornerColor, segStart, cornerLen);
        pos += cornerLen;
    }

    // === Segment 5: GFX right edge, bottom to top ===
    {
        int segStart = pos;
        int segEnd = pos + rightLen;
        int16_t edgeX = screenW - barThick;
        int16_t startY = screenH - cornerR;

        if (filledLen >= segEnd) {
            gfx->fillRect(edgeX, cornerR, barThick, rightLen, fillColor);
        } else if (filledLen <= segStart) {
            gfx->fillRect(edgeX, cornerR, barThick, rightLen, emptyColor);
        } else {
            int fillPx = filledLen - segStart;
            // Drawing bottom to top, so filled is at bottom
            gfx->fillRect(edgeX, startY - fillPx, barThick, fillPx, fillColor);
            gfx->fillRect(edgeX, cornerR, barThick, rightLen - fillPx, emptyColor);
        }
        pos = segEnd;
    }

    // === Segment 6: GFX top-right corner arc ===
    {
        int segStart = pos;
        int segMid = pos + cornerLen / 2;
        uint16_t cornerColor = (filledLen >= segMid) ? fillColor : emptyColor;
        drawCornerArc(0, -M_PI / 2, screenW - cornerR, cornerR, cornerColor, segStart, cornerLen);
        pos += cornerLen;
    }

    // === Segment 7: GFX top edge, right to left ===
    {
        int segStart = pos;
        int segEnd = pos + topLen;

        if (filledLen >= segEnd) {
            gfx->fillRect(cornerR, 0, topLen, barThick, fillColor);
        } else if (filledLen <= segStart) {
            gfx->fillRect(cornerR, 0, topLen, barThick, emptyColor);
        } else {
            int fillPx = filledLen - segStart;
            // Drawing right to left, so filled is on right
            gfx->fillRect(screenW - cornerR - fillPx, 0, fillPx, barThick, fillColor);
            gfx->fillRect(cornerR, 0, topLen - fillPx, barThick, emptyColor);
        }
        pos = segEnd;
    }

    // === Segment 8: GFX top-left corner arc ===
    {
        int segStart = pos;
        int segMid = pos + cornerLen / 2;
        uint16_t cornerColor = (filledLen >= segMid) ? fillColor : emptyColor;
        drawCornerArc(-M_PI / 2, -M_PI, cornerR, cornerR, cornerColor, segStart, cornerLen);
        pos += cornerLen;
    }

    // === Segment 9: GFX left edge, top to middle ===
    {
        int segStart = pos;
        int segEnd = pos + otherHalfLeftLen;

        if (filledLen >= segEnd) {
            gfx->fillRect(0, cornerR, barThick, otherHalfLeftLen, fillColor);
        } else if (filledLen <= segStart) {
            gfx->fillRect(0, cornerR, barThick, otherHalfLeftLen, emptyColor);
        } else {
            int fillPx = filledLen - segStart;
            gfx->fillRect(0, cornerR, barThick, fillPx, fillColor);
            gfx->fillRect(0, cornerR + fillPx, barThick, otherHalfLeftLen - fillPx, emptyColor);
        }
    }

    if (manageWrite) gfx->endWrite();
}

void renderBreathingBars() {
    // Render two thin horizontal bars with breathing brightness
    float brightness = sleepBehavior.getBreathingBrightness();

    // Convert brightness to color (cyan-ish like the eyes)
    uint8_t r = (uint8_t)(0 * brightness);
    uint8_t g = (uint8_t)(200 * brightness);
    uint8_t b = (uint8_t)(255 * brightness);
    uint16_t barColor = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

    // Clear combined buffer to black
    renderer.clearBuffer(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT);

    // Draw bars for sleep (XY swapped for screen rotation)
    // Thin in buffer X, tall in buffer Y, centered horizontally
    int16_t barThickness = 6;                   // Thin dimension
    int16_t barLength = BASE_EYE_HEIGHT * 3/4;  // Long dimension
    int16_t centerX = COMBINED_BUF_WIDTH / 2;

    // Left eye bar (upper position in buffer Y)
    int16_t leftBarStartY = leftEyePos.baseY - barLength / 2;
    int16_t leftBarStartX = centerX - barThickness / 2;
    for (int16_t y = leftBarStartY; y < leftBarStartY + barLength; y++) {
        for (int16_t x = leftBarStartX; x < leftBarStartX + barThickness; x++) {
            if (x >= 0 && x < COMBINED_BUF_WIDTH && y >= 0 && y < COMBINED_BUF_HEIGHT) {
                eyeBuffer[y * COMBINED_BUF_WIDTH + x] = barColor;
            }
        }
    }

    // Right eye bar (lower position in buffer Y)
    int16_t rightBarStartY = rightEyePos.baseY - barLength / 2;
    int16_t rightBarStartX = centerX - barThickness / 2;
    for (int16_t y = rightBarStartY; y < rightBarStartY + barLength; y++) {
        for (int16_t x = rightBarStartX; x < rightBarStartX + barThickness; x++) {
            if (x >= 0 && x < COMBINED_BUF_WIDTH && y >= 0 && y < COMBINED_BUF_HEIGHT) {
                eyeBuffer[y * COMBINED_BUF_WIDTH + x] = barColor;
            }
        }
    }

    // Push to display
    gfx->startWrite();
    gfx->draw16bitRGBBitmap(leftEyePos.bufX, leftEyePos.bufY,
                            eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT);
    gfx->endWrite();
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n=== Robot Eyes (Touch Response) ===");
    Serial.println("Tap to change expression, hold 2s to pet");

    // Allocate combined eye buffer in PSRAM
    size_t bufSize = COMBINED_BUF_WIDTH * COMBINED_BUF_HEIGHT * sizeof(uint16_t);
    eyeBuffer = (uint16_t *)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);

    if (!eyeBuffer) {
        Serial.println("PSRAM alloc failed, using internal RAM");
        eyeBuffer = (uint16_t *)malloc(bufSize);
    }

    if (!eyeBuffer) {
        Serial.println("Buffer alloc failed!");
        while (1) delay(1000);
    }

    Serial.printf("Combined eye buffer: %dx%d (%d bytes)\n",
                  COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT, bufSize);

    Wire.begin(IIC_SDA, IIC_SCL);
    Wire.setClock(400000);

    if (!gfx->begin()) {
        Serial.println("Display init failed!");
        while (1) delay(1000);
    }

    gfx->setBrightness(255);
    gfx->fillScreen(BG_COLOR);

    initEyePositions();

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

    // Initialize pomodoro timer and connect to settings menu
    pomodoroTimer.begin();
    settingsMenu.setPomodoroTimer(&pomodoroTimer);

    // Apply initial settings from saved preferences
    audioPlayer.setVolume(settingsMenu.getVolume());
    gfx->setBrightness((settingsMenu.getBrightness() * 255) / 100);
    // Mic sensitivity slider controls gain (0-42dB), threshold slider controls detection level
    audioPlayer.setMicGain(settingsMenu.getMicSensitivity());
    audio.setThreshold(settingsMenu.getMicThreshold() / 100.0f);
    renderer.setColor(settingsMenu.getColorRGB565());

    Serial.println("2-finger tap to open settings menu");

    // Initialize WiFi manager
    wifiManager.begin(BOOT_BUTTON_PIN);
    wifiWasEnabled = settingsMenu.isWiFiEnabled();
    lastGmtOffsetHours = settingsMenu.getGmtOffsetHours();

    // Check if WiFi is completely disabled in settings
    if (!settingsMenu.isWiFiEnabled()) {
        Serial.println("WiFi disabled in settings - staying offline");
        wifiManager.disable();
    } else if (wifiManager.hasCredentials()) {
        Serial.println("Connecting to saved WiFi...");
        wifiManager.connectToSavedWiFi();
    } else {
        Serial.println("No WiFi credentials - starting AP mode");
        wifiManager.startAPMode();
        // Start captive portal DNS server for automatic redirect
        captivePortal.begin(WIFI_AP_IP);
        Serial.println("Captive portal started");

        // Show first-boot setup screen only if user hasn't chosen offline mode yet
        if (!settingsMenu.isOfflineModeConfigured()) {
            isShowingWiFiSetup = true;
            Serial.println("First boot - showing WiFi setup screen");
        }
    }

    // Start web server (works in both AP and STA mode)
    webServer.begin(&settingsMenu, &pomodoroTimer, &wifiManager);
    webServer.setExpressionCallback(onWebExpressionPreview);
    webServer.setAudioTestCallback(onWebAudioTest);
    webServer.setMoodGetterCallback(getCurrentMood);

    // Initialize gaze tweeners
    gazeX.setSmoothTime(0.15f);
    gazeY.setSmoothTime(0.15f);

    // Initialize eye shape tweeners
    leftEyeTweener.setSmoothTime(0.2f);
    rightEyeTweener.setSmoothTime(0.2f);

    // Initialize random joy timer (first joy in 10-30 minutes)
    nextJoyTime = millis() + JOY_MIN_INTERVAL + random(JOY_MAX_INTERVAL - JOY_MIN_INTERVAL);
    Serial.printf("First joy scheduled in %lu minutes\n", (nextJoyTime - millis()) / 60000);

    // Initialize micro-expression timer (first micro-expression in 2-5 minutes)
    nextMicroExprTime = millis() + MICRO_EXPR_MIN_INTERVAL + random(MICRO_EXPR_MAX_INTERVAL - MICRO_EXPR_MIN_INTERVAL);
    Serial.printf("First micro-expression in %lu minutes\n", (nextMicroExprTime - millis()) / 60000);

    // Start with neutral expression
    setExpression(Expression::Neutral);

    // Snap tweeners to initial state
    leftEyeTweener.snapTo(leftEyeBase);
    rightEyeTweener.snapTo(rightEyeBase);

    // Get initial current shapes
    leftEyeTweener.getCurrentShape(leftEye);
    rightEyeTweener.getCurrentShape(rightEye);

    // Initial render to combined buffer
    // Clear once, then render both eyes without clearing
    renderer.clearBuffer(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT);
    renderer.renderToBuf(leftEye, eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                         leftEyePos.baseX, leftEyePos.baseY, true, false);
    renderer.renderToBuf(rightEye, eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                         rightEyePos.baseX, rightEyePos.baseY, false, false);

    gfx->draw16bitRGBBitmap(leftEyePos.bufX, leftEyePos.bufY,
                            eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT);

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

    // Update WiFi state machine (handles connection, reconnection, factory reset)
    wifiManager.update();

    // Trigger NTP sync when WiFi first connects
    bool wifiNowConnected = wifiManager.isConnected();
    if (wifiNowConnected && !wifiWasConnected) {
        // WiFi just connected - sync NTP with configured timezone
        wifiManager.syncNTP(settingsMenu.getGmtOffsetHours() * 3600L);
    }
    wifiWasConnected = wifiNowConnected;

    // Update captive portal DNS server (only when in AP mode)
    if (wifiManager.isAPMode()) {
        if (!captivePortal.isRunning()) {
            // Start captive portal when entering AP mode (e.g., after connection failure)
            captivePortal.begin(WIFI_AP_IP);
            Serial.println("Captive portal started");
        }
        captivePortal.update();
    } else if (captivePortal.isRunning()) {
        // Stop captive portal when leaving AP mode
        captivePortal.stop();
        Serial.println("Captive portal stopped");
    }

    // Apply settings changes from web interface
    if (webServer.hasSettingsChange()) {
        audioPlayer.setVolume(settingsMenu.getVolume());
        gfx->setBrightness((settingsMenu.getBrightness() * 255) / 100);
        audioPlayer.setMicGain(settingsMenu.getMicSensitivity());
        audio.setThreshold(settingsMenu.getMicThreshold() / 100.0f);
        renderer.setColor(settingsMenu.getColorRGB565());
        webServer.clearSettingsChange();
    }

    // Handle timezone change - re-sync NTP with new offset
    int8_t currentGmtOffset = settingsMenu.getGmtOffsetHours();
    if (currentGmtOffset != lastGmtOffsetHours) {
        lastGmtOffsetHours = currentGmtOffset;
        if (wifiManager.isConnected()) {
            Serial.printf("Timezone changed to UTC%+d - re-syncing NTP\n", currentGmtOffset);
            wifiManager.syncNTP(currentGmtOffset * 3600L);
        }
    }

    // Handle WiFi enable/disable changes from device settings menu
    bool wifiNowEnabled = settingsMenu.isWiFiEnabled();
    if (wifiNowEnabled != wifiWasEnabled) {
        if (wifiNowEnabled) {
            // WiFi was just enabled
            Serial.println("WiFi enabled from settings");
            wifiManager.enable();
            // Start captive portal if in AP mode
            if (wifiManager.isAPMode() && !captivePortal.isRunning()) {
                captivePortal.begin(WIFI_AP_IP);
            }
        } else {
            // WiFi was just disabled
            Serial.println("WiFi disabled from settings");
            if (captivePortal.isRunning()) {
                captivePortal.stop();
            }
            wifiManager.disable();
        }
        wifiWasEnabled = wifiNowEnabled;
        needFullScreenClear = true;
    }

    // Time tracking - advance clock every minute and trigger display
    if (now - lastTimeTick >= TIME_TICK_INTERVAL) {
        lastTimeTick = now;
        settingsMenu.tickMinute();

        // Trigger time display (unless menu is open or sleeping)
        if (!settingsMenu.isOpen() && !sleepBehavior.isSleeping()) {
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
    if (imuEvent == ImuEvent::PickedUp && !isPetted && !isImuReacting) {
        // Trigger scared expression when picked up
        preGestureExpression = currentExpression;
        setExpression(Expression::Scared);
        isImuReacting = true;
        imuReactionStart = now;
        debugExpressionActive = false;  // Cancel debug mode
            showingJoy = false;  // Cancel joy mode
        audioPlayer.play("/pick up.mp3");
        Serial.println("Picked up - playing pick up.mp3");
    } else if (imuEvent == ImuEvent::ShookHard && !isPetted) {
        // Trigger dazed expression (spirals) when shaken
        if (!isImuReacting) {
            preGestureExpression = currentExpression;
        }
        setExpression(Expression::Dazed);
        isImuReacting = true;
        imuReactionStart = now;
        debugExpressionActive = false;  // Cancel debug mode
            showingJoy = false;  // Cancel joy mode
        audioPlayer.play("/confused.mp3");
        Serial.println("Shaken - showing spirals, playing confused.mp3");
    } else if (imuEvent == ImuEvent::Knocked && !isPetted) {
        // Trigger dizzy expression (stars) when knocked
        if (!isImuReacting) {
            preGestureExpression = currentExpression;
        }
        setExpression(Expression::Dizzy);
        isImuReacting = true;
        imuReactionStart = now;
        debugExpressionActive = false;  // Cancel debug mode
            showingJoy = false;  // Cancel joy mode
        audioPlayer.play("/confused.mp3");
        Serial.println("Knocked - showing stars, playing confused.mp3");
    }

    // Handle orientation-based expressions (face-down, tilted long)
    Orientation currentOrientation = imu.getOrientation();
    if (currentOrientation != lastOrientation) {
        // Orientation changed
        if (currentOrientation == Orientation::FaceDown && !isPetted && !isImuReacting) {
            // Face-down: show hiding/sleepy expression
            if (!showingOrientationExpr) {
                expressionBeforeOrientation = currentExpression;
            }
            setExpression(Expression::Sleepy);  // Heavy lids = hiding
            showingOrientationExpr = true;
            Serial.println("Face-down - showing hiding expression");
        } else if (currentOrientation == Orientation::TiltedLong && !isPetted && !isImuReacting) {
            // Tilted for long time: show uncomfortable/squint expression
            if (!showingOrientationExpr) {
                expressionBeforeOrientation = currentExpression;
            }
            setExpression(Expression::Squint);
            showingOrientationExpr = true;
            Serial.println("Tilted long - showing uncomfortable expression");
        } else if (currentOrientation == Orientation::Normal && showingOrientationExpr) {
            // Return to normal orientation
            setExpression(expressionBeforeOrientation);
            showingOrientationExpr = false;
            Serial.println("Orientation normal - reverting expression");
        }
        lastOrientation = currentOrientation;
    }

    // Return to normal after IMU reaction duration
    if (isImuReacting && !isPetted && (now - imuReactionStart > IMU_REACTION_DURATION)) {
        setExpression(preGestureExpression);
        isImuReacting = false;
    }

    // Auto-revert debug expression after 5 seconds
    if (debugExpressionActive && (now - debugExpressionStart > DEBUG_EXPRESSION_DURATION)) {
        setExpression(expressionBeforeDebugTap);
        debugExpressionActive = false;
        Serial.println("Debug expression reverted");
    }

    // Update audio player (streams audio chunks)
    audioPlayer.update();

    // Update pomodoro timer
    bool pomodoroChanged = pomodoroTimer.update(deltaTime);
    PomodoroState pomodoroState = pomodoroTimer.getState();

    // Handle pomodoro state changes
    if (pomodoroState != lastPomodoroState) {
        // Reset progress bar cache on any state change (forces redraw)
        lastRenderedFilledLen = -1;

        if (pomodoroState == PomodoroState::Working) {
            // Starting work session - trigger concentrate animation
            if (lastPomodoroState == PomodoroState::Idle) {
                expressionBeforePomodoro = currentExpression;
            }
            // Start concentrate animation: eyes close → snap open → settle to Focused
            concentratePhase = 1;
            concentrateStart = now;
            setExpression(Expression::Sleepy);  // Phase 1: eyes closing
            pomodoroExpressActive = true;
            showingJoy = false;  // Clear any previous joy animation
            Serial.println("Pomodoro: Work starting - Concentrate animation");
        } else if (pomodoroState == PomodoroState::ShortBreak || pomodoroState == PomodoroState::LongBreak) {
            // Starting break - relaxed expression (after celebration ends)
            setExpression(Expression::Content);
            pomodoroExpressActive = true;
            concentratePhase = 0;
            showingJoy = false;  // Clear joy animation from celebration
            joyBouncePhase = 0.0f;  // Reset bounce for Content animation
            // Reset joy timer to prevent immediate random joy trigger
            nextJoyTime = now + JOY_MIN_INTERVAL + random(JOY_MAX_INTERVAL - JOY_MIN_INTERVAL);
            Serial.println("Pomodoro: Break started - Content expression");
        } else if (pomodoroState == PomodoroState::Celebration) {
            // Session complete - only Joy for work completion, Content for break completion
            concentratePhase = 0;
            if (lastPomodoroState == PomodoroState::Working) {
                // Work complete - celebrate with Joy and bounce animation!
                setExpression(Expression::Joy);
                showingJoy = true;         // Enable bounce animation
                joyBouncePhase = 0.0f;     // Reset bounce phase
                joyStart = now;            // Set start time for duration tracking
                audioPlayer.play("/joy.mp3");
                Serial.println("Pomodoro: Work complete - Joy celebration with bounce!");
            } else {
                // Break complete - just Content with bounce animation
                setExpression(Expression::Content);
                showingJoy = false;        // Use Content bounce instead
                joyBouncePhase = 0.0f;     // Reset bounce for Content
                Serial.println("Pomodoro: Break complete - Content expression");
            }
            pomodoroExpressActive = true;
        } else if (pomodoroState == PomodoroState::Idle && pomodoroExpressActive) {
            // Timer stopped - restore expression and start progress bar clear animation
            setExpression(expressionBeforePomodoro);
            pomodoroExpressActive = false;
            concentratePhase = 0;
            showingJoy = false;  // Clear any joy animation
            // Start progressive clear animation
            progressBarClearing = true;
            clearAnimStart = now;
            clearAnimProgress = 0.0f;
            Serial.println("Pomodoro: Stopped - clearing progress bar");
        }
        lastPomodoroState = pomodoroState;
    }

    // Update concentrate animation phases
    if (concentratePhase > 0) {
        uint32_t elapsed = now - concentrateStart;
        if (concentratePhase == 1 && elapsed >= CONCENTRATE_CLOSE_DURATION) {
            // Phase 1 done → Phase 2: Eyes snap open wide (Alert)
            concentratePhase = 2;
            concentrateStart = now;
            setExpression(Expression::Alert);
            Serial.println("Pomodoro: Concentrate - Eyes wide!");
        } else if (concentratePhase == 2 && elapsed >= CONCENTRATE_ALERT_DURATION) {
            // Phase 2 done → Phase 3: Settle into Focused
            concentratePhase = 0;
            setExpression(Expression::Focused);
            Serial.println("Pomodoro: Concentrate complete - Focused");
        }
    }

    // Pomodoro tick sound in last 60 seconds
    if (pomodoroTimer.isActive() && pomodoroTimer.isTickingEnabled() && pomodoroTimer.isLastMinute()) {
        uint32_t remaining = pomodoroTimer.getRemainingSeconds();
        // Tick every second, but only if not already playing (avoid race condition)
        if (remaining != (lastPomodoroTick / 1000)) {
            lastPomodoroTick = remaining * 1000;
            if (!audioPlayer.isPlaying()) {
                Serial.printf("Tick: %lu seconds remaining\n", remaining);
                audioPlayer.play("/tick.mp3");
            } else {
                Serial.printf("Tick skipped (audio busy): %lu seconds\n", remaining);
            }
        }
    }

    // Update audio handler for microphone monitoring (full-duplex with playback)
    AudioEvent audioEvent = audio.update(deltaTime);

    // Debug: show mic level periodically
    static uint32_t lastMicDebug = 0;
    if (now - lastMicDebug > 1000) {  // Every second
        int slider = settingsMenu.getMicSensitivity();

        // Calculate effective gain with center-zero system (50 = 0dB)
        float effectiveGainDb;
        if (slider < 50) {
            // Left side: attenuation
            float t = slider / 50.0f;
            float attenuation = 0.0625f + t * (1.0f - 0.0625f);
            effectiveGainDb = 20.0f * log10f(attenuation);
        } else {
            // Right side: positive gain
            int gainRange = slider - 50;
            int gainDb = (gainRange < 7) ? 0 : (gainRange < 14) ? 6 : (gainRange < 21) ? 12 :
                        (gainRange < 28) ? 18 : (gainRange < 35) ? 24 : (gainRange < 42) ? 30 :
                        (gainRange < 49) ? 36 : 42;
            effectiveGainDb = gainDb;
        }

        Serial.printf("Mic level: %.3f (gain: %+.1fdB, threshold: %.2f, slider: %d)\n",
                      audio.getLevel(), effectiveGainDb, settingsMenu.getMicThreshold() / 100.0f, slider);
        lastMicDebug = now;
    }

    // Handle "too loud" audio event - show irritated expression
    if (audioEvent == AudioEvent::TooLoud && !isPetted && !isImuReacting && !showingLove && !showingIrritated) {
        expressionBeforeIrritated = currentExpression;
        setExpression(Expression::Grumpy);
        showingIrritated = true;
        irritatedStart = now;
        debugExpressionActive = false;
        showingJoy = false;
        microExprActive = false;
        Serial.println("Too loud! Showing irritated expression");
    }

    // Update irritated timer
    if (showingIrritated) {
        if (now - irritatedStart >= IRRITATED_DURATION) {
            setExpression(expressionBeforeIrritated);
            showingIrritated = false;
            Serial.println("Irritated done, returning to previous expression");
        }
    }

    // Update love hearts timer
    if (showingLove) {
        if (now - loveStart >= LOVE_DURATION) {
            setExpression(expressionBeforeLove);
            showingLove = false;
            Serial.println("Love hearts done");
        }
    }

    // Update shape animation phase (for rotating stars, pulsing hearts)
    shapeAnimPhase += deltaTime * 0.5f;  // Rotate once every 2 seconds
    if (shapeAnimPhase >= 1.0f) {
        shapeAnimPhase -= 1.0f;
    }

    // Check for interaction/motion states
    bool hasInteraction = isTouching || (audioEvent != AudioEvent::None);
    bool hasMotion = (imuEvent == ImuEvent::PickedUp) || (imuEvent == ImuEvent::ShookHard) || (imuEvent == ImuEvent::Knocked);

    // Notify idle behavior of activity for yawn timing
    if (isTouching || hasMotion || (audioEvent != AudioEvent::None)) {
        idle.notifyActivity();
    }

    // Track sleep state before update for wake detection
    bool wasAsleep = sleepBehavior.isSleeping() || sleepBehavior.isDrowsy();
    bool wasFallingAsleep = sleepBehavior.isFallingAsleep();

    // Update sleep behavior
    sleepBehavior.update(deltaTime, hasInteraction, hasMotion);

    // Play confused sound when waking from sleep/drowsy due to shaking/knocking
    bool isAwakeNow = !sleepBehavior.isSleeping() && !sleepBehavior.isDrowsy();
    if (wasAsleep && isAwakeNow && (imuEvent == ImuEvent::ShookHard || imuEvent == ImuEvent::Knocked)) {
        audioPlayer.play("/confused.mp3");
        Serial.println("Woke from sleep by shaking/knock - playing confused.mp3");
    }

    // Play yawn sound once when entering falling asleep state
    if (!wasFallingAsleep && sleepBehavior.isFallingAsleep()) {
        audioPlayer.play("/yawn.mp3");
        Serial.println("Falling asleep - playing yawn.mp3");
    }

    // Apply brightness from settings (with petting pulse override)
    int baseBrightness = (settingsMenu.getBrightness() * 255) / 100;
    if (isPetted) {
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

    // Handle yawn behavior (30-40 min idle)
    if (idle.shouldYawn() && !isPetted && !isImuReacting && !showingIrritated && !showingJoy) {
        preGestureExpression = currentExpression;
        setExpression(Expression::Yawn);
        isImuReacting = true;  // Reuse for recovery timing
        imuReactionStart = now;
        debugExpressionActive = false;  // Cancel debug mode
            showingJoy = false;  // Cancel joy mode
        // audioPlayer.play("/yawn.mp3");
        Serial.println("Yawn triggered (sound disabled)");
    }

    // Handle random joy behavior (every 10-30 minutes when idle, not during pomodoro)
    if (!showingJoy && now >= nextJoyTime && !sleepBehavior.isSleeping() && !sleepBehavior.isDrowsy() &&
        !isPetted && !isImuReacting && !showingIrritated && !showingLove && !pomodoroTimer.isActive()) {
        // Trigger joy!
        expressionBeforeJoy = currentExpression;
        setExpression(Expression::Joy);
        showingJoy = true;
        joyStart = now;
        joyBouncePhase = 0.0f;
        debugExpressionActive = false;
        // Schedule next joy NOW (prevents re-trigger if this one is cancelled early)
        nextJoyTime = now + JOY_MIN_INTERVAL + random(JOY_MAX_INTERVAL - JOY_MIN_INTERVAL);
        audioPlayer.play("/joy.mp3");
        Serial.printf("Joy triggered! Next joy in %lu minutes\n", (nextJoyTime - now) / 60000);
    }

    // Update joy animation
    if (showingJoy) {
        // Update bounce phase (3 bounces over the duration)
        joyBouncePhase += deltaTime * 3.0f;  // 3 bounces per second

        // Check if joy duration is over
        if (now - joyStart > JOY_DURATION) {
            showingJoy = false;
            setExpression(expressionBeforeJoy);
            Serial.println("Joy ended");
        }
    }

    // Update Content bounce animation (for pomodoro breaks)
    if (currentExpression == Expression::Content && pomodoroTimer.isActive()) {
        joyBouncePhase += deltaTime * 3.0f;  // Same bounce rate as Joy
    }

    // Update petting bounce animation
    if (isPetted) {
        joyBouncePhase += deltaTime * 3.0f;  // Same bounce rate as Joy
    }

    //=========================================================================
    // Micro-Expression Behavior (random idle personality moments)
    //=========================================================================

    // Trigger random micro-expression
    if (!microExprActive && now >= nextMicroExprTime && !sleepBehavior.isSleeping() &&
        !sleepBehavior.isDrowsy() && !isPetted && !isImuReacting &&
        !showingIrritated && !showingLove && !showingJoy &&
        !debugExpressionActive && currentExpression == Expression::Neutral) {
        triggerRandomMicroExpression();
        // Schedule next micro-expression
        nextMicroExprTime = now + MICRO_EXPR_MIN_INTERVAL +
                           random(MICRO_EXPR_MAX_INTERVAL - MICRO_EXPR_MIN_INTERVAL);
    }

    // Update micro-expression
    if (microExprActive) {
        uint32_t duration = getMicroExprDuration();
        uint32_t elapsed = now - microExprStart;
        microExprPhase = (float)elapsed / (float)duration;

        // Check if done
        if (elapsed >= duration) {
            microExprActive = false;
            currentMicroExpr = MicroExpressionType::None;
            setExpression(expressionBeforeMicro);
            Serial.println("Micro-expression done");
        }
    }

    // Cancel micro-expression on interaction
    if (microExprActive && (isPetted || isImuReacting || showingLove ||
        showingIrritated || showingJoy)) {
        microExprActive = false;
        currentMicroExpr = MicroExpressionType::None;
        Serial.println("Micro-expression cancelled by interaction");
    }

    // Handle sleep state transitions
    if (sleepBehavior.isWakingUp() && !isImuReacting && !showingIrritated) {
        // Show startled expression when waking up
        setExpression(Expression::Startled);
    } else if (sleepBehavior.isDrowsy() && !isPetted && !isImuReacting && !showingIrritated) {
        // Check for snap-wide (brief alertness during drowsy)
        if (sleepBehavior.isSnapWide()) {
            // Briefly show alert/neutral during snap-wide
            if (currentExpression == Expression::Sleepy) {
                setExpression(Expression::Neutral);
            }
        } else {
            // Blend toward sleepy expression based on drowsiness level
            float drowsiness = sleepBehavior.getDrowsiness();
            if (drowsiness > 0.5f && currentExpression != Expression::Sleepy) {
                setExpression(Expression::Sleepy);
            }
        }
    }

    // Expression timeout safety - return to Neutral if stuck in non-neutral expression
    // Only applies when no active behaviors are controlling the expression
    if (currentExpression != Expression::Neutral &&
        !isPetted && !isImuReacting && !showingLove && !showingJoy && !microExprActive &&
        !showingIrritated && !debugExpressionActive &&
        !sleepBehavior.isDrowsy() && !sleepBehavior.isWakingUp() &&
        (now - lastExpressionChange > EXPRESSION_TIMEOUT)) {
        Serial.println("Expression timeout - returning to Neutral");
        setExpression(Expression::Neutral);
    }

    // Determine current render mode for full-screen clear on transitions
    // Modes: 0=eyes, 1=menu, 2=countdown, 3=sleep, 4=timeDisplay, 5=wifiSetup
    int currentRenderMode = 0;  // Default: eyes
    if (isShowingWiFiSetup) {
        currentRenderMode = 5;  // wifiSetup (first-boot setup screen with buttons)
    } else if (sleepBehavior.isSleeping()) {
        currentRenderMode = 3;  // sleep
    } else if (settingsMenu.isOpen()) {
        currentRenderMode = 1;  // menu
    } else if (pomodoroTimer.isActive() &&
               pomodoroState != PomodoroState::Celebration &&
               pomodoroState != PomodoroState::WaitingForTap &&
               concentratePhase == 0) {
        currentRenderMode = 2;  // countdown (only after concentrate animation)
    } else if (isShowingTime) {
        currentRenderMode = 4;  // timeDisplay
    }

    // Check for mode change - need full physical screen clear
    if (currentRenderMode != lastRenderMode) {
        needFullScreenClear = true;
        Serial.printf("Render mode change: %d -> %d (full screen clear)\n", lastRenderMode, currentRenderMode);
        lastRenderMode = currentRenderMode;
    }

    // Execute full screen clear if needed (clears ENTIRE physical display including margins)
    if (needFullScreenClear) {
        gfx->startWrite();
        gfx->fillScreen(0);  // Clear entire 368x448 physical screen
        gfx->endWrite();
        needFullScreenClear = false;
        // Also reset dirty rect tracking since we just cleared everything
        prevLeftRect.valid = false;
        prevRightRect.valid = false;
        prevFrameWasMenu = false;
        lastRenderedFilledLen = -1;  // Force progress bar redraw
    }

    // First-boot WiFi setup screen with "Configure WiFi" and "Use Offline" buttons
    if (isShowingWiFiSetup) {
        // Handle touch for button selection
        // Screen is 336x416 (buffer coords), split into two button regions
        // Top half (y < 208): "Configure WiFi"
        // Bottom half (y >= 208): "Use Offline"
        if (isTouching && !wifiSetupTouchWasActive) {
            // touchY is in screen coords (0-448), need to convert to buffer coords
            // Buffer Y maps to screen X after 90° CCW rotation
            // Use touchX as the effective Y position (due to rotation)
            int16_t effectiveY = touchX;  // Screen X = Buffer Y (rotated)

            if (effectiveY < COMBINED_BUF_HEIGHT / 2) {
                // Top half - Configure WiFi
                Serial.println("Configure WiFi selected - keeping AP mode for setup");
                isShowingWiFiSetup = false;
                needFullScreenClear = true;
            } else {
                // Bottom half - Use Offline
                Serial.println("Use Offline selected - eyes will show, AP stays running");
                settingsMenu.setOfflineModeConfigured(true);
                isShowingWiFiSetup = false;
                needFullScreenClear = true;
            }
        }
        wifiSetupTouchWasActive = isTouching;

        // Render first-boot setup screen
        settingsMenu.renderFirstBootSetup(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                                          renderer.getColor());
        gfx->startWrite();
        gfx->draw16bitRGBBitmap(leftEyePos.bufX, leftEyePos.bufY,
                                eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT);
        gfx->endWrite();
        return;
    }

    // If sleeping, render breathing bars and skip normal eye rendering
    if (sleepBehavior.isSleeping()) {
        renderBreathingBars();
        return;
    }

    // During active pomodoro (except celebration), show countdown timer instead of eyes
    // BUT: if concentrate animation is playing, show eyes first
    if (pomodoroTimer.isActive() &&
        pomodoroState != PomodoroState::Celebration &&
        pomodoroState != PomodoroState::WaitingForTap &&
        concentratePhase == 0) {
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

        // Draw progress bar first, then blit only the safe central region of the buffer
        // This prevents the buffer from overlapping the 42px rounded corners
        // Safe region: buffer offset (26,26), size (284,364), blits to screen (42,42)
        const int16_t cornerMargin = 42 - 16;  // 26px offset from buffer edge to avoid corners
        const int16_t safeW = COMBINED_BUF_WIDTH - 2 * cornerMargin;   // 284
        const int16_t safeH = COMBINED_BUF_HEIGHT - 2 * cornerMargin;  // 364

        gfx->startWrite();
        renderPomodoroProgressBar(pomodoroTimer.getProgress(), false, true);  // Progressive corners
        // Blit only the safe central region that doesn't overlap corners
        DirtyRect safeRegion = {cornerMargin, cornerMargin, safeW, safeH, true};
        blitRegion(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                   leftEyePos.bufX, leftEyePos.bufY, safeRegion, false);  // false = don't manage write
        gfx->endWrite();

        // Mark that we need full blit when exiting this mode
        prevFrameWasMenu = true;
        prevLeftRect.valid = false;
        prevRightRect.valid = false;
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
            gfx->startWrite();
            gfx->draw16bitRGBBitmap(leftEyePos.bufX, leftEyePos.bufY,
                                    eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT);
            gfx->endWrite();
            needFullBlitAfterTime = true;
            return;
        } else {
            // Time display finished
            isShowingTime = false;
        }
    }

    // Note: needFullBlitAfterTime is handled in the rendering section below
    // to ensure full screen blit clears time display artifacts

    // Update blink animation
    updateBlink();

    // Update gaze
    updateGaze();

    // Build target shapes from base expression + dynamic state
    float openness = getBlinkOpenness();

    // Get combined gaze (touch target or idle scanning) + micro-movement + tilt
    float totalGazeX = gazeX.getValue() + idle.getMicroX();
    float totalGazeY = gazeY.getValue() + idle.getMicroY();

    // Add tilt-based gaze if enabled and not touching
    // Axes swapped for 90° CCW rotation: TiltY → vertical (X), TiltX → horizontal (Y)
    if (imu.isTiltGazeEnabled() && !isTouching) {
        totalGazeX += imu.getTiltGazeY() * 0.5f;  // Forward/back tilt → look up/down
        totalGazeY += imu.getTiltGazeX() * 0.5f;  // Left/right tilt → look left/right
    }

    // Copy base expression and apply dynamic parameters
    leftEyeTarget = leftEyeBase;
    leftEyeTarget.openness *= openness;
    leftEyeTarget.offsetX += totalGazeX;
    leftEyeTarget.offsetY += totalGazeY;

    rightEyeTarget = rightEyeBase;
    rightEyeTarget.openness *= openness;
    rightEyeTarget.offsetX += totalGazeX;
    rightEyeTarget.offsetY += totalGazeY;

    // Petting bobbing effect: gently oscillate lid closure
    if (isPetted) {
        // Bob between topLid 0.35 and 0.55 (centered around 0.45)
        float bobAmount = 0.1f * sinf(pettingPulsePhase * 2.0f * PI);
        leftEyeTarget.topLid += bobAmount;
        rightEyeTarget.topLid += bobAmount;
    }

    // Apply time-of-day mood lid offset (heavier lids at night)
    if (moodModifiers.baseLidOffset > 0.0f && !isPetted && !sleepBehavior.isDrowsy()) {
        leftEyeTarget.topLid += moodModifiers.baseLidOffset;
        rightEyeTarget.topLid += moodModifiers.baseLidOffset;
    }

    // Side-looking squint effect: eye in direction of gaze narrows, opposite widens
    // totalGazeY: positive = looking toward right eye, negative = toward left eye
    float squintAmount = totalGazeY * 0.25f;  // Subtle effect
    leftEyeTarget.height *= (1.0f + squintAmount);   // Widen when looking down (toward right)
    rightEyeTarget.height *= (1.0f - squintAmount);  // Narrow when looking down
    // Add slight lid closure to the narrowing eye
    if (squintAmount > 0.1f) {
        rightEyeTarget.topLid += squintAmount * 0.3f;
    } else if (squintAmount < -0.1f) {
        leftEyeTarget.topLid += (-squintAmount) * 0.3f;
    }

    // Micro-expression animation effects (for animation-based types)
    if (microExprActive) {
        // Apply gaze offset for animation-based micro-expressions
        float microGazeX = 0, microGazeY = 0;
        getMicroExprGazeOffset(microGazeX, microGazeY);
        leftEyeTarget.offsetX += microGazeX;
        leftEyeTarget.offsetY += microGazeY;
        rightEyeTarget.offsetX += microGazeX;
        rightEyeTarget.offsetY += microGazeY;

        // Apply openness modifier for wink/sigh
        float leftOpenness = getMicroExprOpenness(true);
        float rightOpenness = getMicroExprOpenness(false);
        leftEyeTarget.openness *= leftOpenness;
        rightEyeTarget.openness *= rightOpenness;
    }

    // Set targets on tweeners
    leftEyeTweener.setTarget(leftEyeTarget);
    rightEyeTweener.setTarget(rightEyeTarget);

    // Update tweeners
    leftEyeTweener.update(deltaTime);
    rightEyeTweener.update(deltaTime);

    // Get current interpolated shapes
    leftEyeTweener.getCurrentShape(leftEye);
    rightEyeTweener.getCurrentShape(rightEye);

    // Apply animation phase for special shapes (stars, hearts)
    leftEye.animPhase = shapeAnimPhase;
    rightEye.animPhase = shapeAnimPhase;

    // Pulse hearts when showing love
    if (showingLove) {
        float pulseScale = 1.0f + 0.15f * sinf(shapeAnimPhase * 4.0f * PI);  // 2 pulses per cycle
        leftEye.height *= pulseScale;
        rightEye.height *= pulseScale;
    }

    // Render to combined buffer
    if (settingsMenu.isOpen()) {
        // Full-screen settings menu needs full clear
        renderer.clearBuffer(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT);
        settingsMenu.render(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                            leftEyePos.bufX, leftEyePos.bufY, audio.getLevel());

        // Apply settings in real-time while menu is open
        audioPlayer.setVolume(settingsMenu.getVolume());
        audioPlayer.setMicGain(settingsMenu.getMicSensitivity());
        audio.setThreshold(settingsMenu.getMicThreshold() / 100.0f);
        renderer.setColor(settingsMenu.getColorRGB565());
        prevFrameWasMenu = true;
        prevLeftRect.valid = false;
        prevRightRect.valid = false;
    } else {
        // Track if we need full blit (after menu closes or time display ends)
        bool needFullBlit = false;

        // Dirty-rect clearing: only clear previous eye regions instead of full buffer
        if (prevFrameWasMenu || needFullBlitAfterTime) {
            // Transitioning from menu or time display - need full clear AND full blit once
            renderer.clearBuffer(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT);
            prevFrameWasMenu = false;
            needFullBlitAfterTime = false;
            needFullBlit = true;  // Must blit entire screen to clear artifacts
            prevLeftRect.valid = false;
            prevRightRect.valid = false;
        } else if (prevLeftRect.valid || prevRightRect.valid) {
            // Clear only previous eye bounding boxes (with extra margin for bounce animation)
            // Bounce is ±15px, so need 20px margin to fully clear
            if (prevLeftRect.valid) {
                clearRect(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                          prevLeftRect.x - 20, prevLeftRect.y - 5,
                          prevLeftRect.w + 40, prevLeftRect.h + 10);
            }
            if (prevRightRect.valid) {
                clearRect(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                          prevRightRect.x - 20, prevRightRect.y - 5,
                          prevRightRect.w + 40, prevRightRect.h + 10);
            }
        } else {
            // First frame or invalid rects - full clear
            renderer.clearBuffer(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT);
            needFullBlit = true;
        }

        // Normal eye rendering with optional bounce animation (Joy, Content, or Petting)
        int16_t bounceOffset = 0;
        bool shouldBounce = showingJoy || isPetted ||
            (currentExpression == Expression::Content && pomodoroTimer.isActive());
        if (shouldBounce) {
            // Bounce up and down (sin oscillates -1 to +1), 15px amplitude each direction
            bounceOffset = (int16_t)(sinf(joyBouncePhase * 2.0f * PI) * 15.0f);
        }

        int16_t leftCX = leftEyePos.baseX - bounceOffset;
        int16_t rightCX = rightEyePos.baseX - bounceOffset;

        renderer.renderToBuf(leftEye, eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                             leftCX, leftEyePos.baseY, true, false);
        renderer.renderToBuf(rightEye, eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                             rightCX, rightEyePos.baseY, false, false);

        // Compute current eye bounding boxes
        DirtyRect curLeftRect = computeEyeRect(leftEye, leftCX, leftEyePos.baseY);
        DirtyRect curRightRect = computeEyeRect(rightEye, rightCX, rightEyePos.baseY);

        // Animate progress bar clearing (when exiting pomodoro mode)
        if (progressBarClearing) {
            uint32_t elapsed = now - clearAnimStart;
            clearAnimProgress = (float)elapsed / CLEAR_ANIM_DURATION;

            if (clearAnimProgress >= 1.0f) {
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

                progressBarClearing = false;
                lastRenderedFilledLen = -1;
                Serial.println("Progress bar clear complete");
            } else {
                // Animate: deplete the bar from current position to empty
                // Use clearAnimProgress to shrink the filled portion
                // Progressive corners = true for smooth corner animation during clear
                float animatedProgress = 1.0f - clearAnimProgress;  // Goes from 1.0 to 0.0
                renderPomodoroProgressBar(animatedProgress, true, true);
            }
        }

        // Clear progress bar edges if needed (instant clear fallback)
        if (needClearProgressBar) {
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

            needClearProgressBar = false;
            Serial.println("Progress bar cleared (instant)");
        }

        if (needFullBlit) {
            // Full blit to clear artifacts
            gfx->startWrite();
            gfx->draw16bitRGBBitmap(leftEyePos.bufX, leftEyePos.bufY,
                                    eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT);
            gfx->endWrite();
        } else {
            // Partial blit: union of prev + current rects with extra margin
            DirtyRect blitRect = unionRect(prevLeftRect, curLeftRect);
            blitRect = unionRect(blitRect, prevRightRect);
            blitRect = unionRect(blitRect, curRightRect);
            // Add margin to ensure we cover any rendering artifacts
            blitRect.x = (blitRect.x > 5) ? blitRect.x - 5 : 0;
            blitRect.y = (blitRect.y > 5) ? blitRect.y - 5 : 0;
            blitRect.w += 10;
            blitRect.h += 10;
            blitRegion(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                       leftEyePos.bufX, leftEyePos.bufY, blitRect);
        }

        // Save current rects for next frame
        prevLeftRect = curLeftRect;
        prevRightRect = curRightRect;

        // Render pomodoro progress bar if active (hide during celebration/waiting/concentrate)
        if (pomodoroTimer.isActive() &&
            pomodoroState != PomodoroState::WaitingForTap &&
            pomodoroState != PomodoroState::Celebration &&
            concentratePhase == 0) {
            renderPomodoroProgressBar(pomodoroTimer.getProgress(), true, true);  // Progressive corners
        }

        return;
    }

    // Full blit for settings menu or special cases
    gfx->startWrite();
    gfx->draw16bitRGBBitmap(leftEyePos.bufX, leftEyePos.bufY,
                            eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT);
    gfx->endWrite();
}
