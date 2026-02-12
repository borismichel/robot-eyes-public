/**
 * @file breathing_exercise.cpp
 * @brief Mindfulness breathing exercise implementation
 */

#include "breathing_exercise.h"
#include "../ui/text_renderer.h"
#include <math.h>

// Screen dimensions (after 90° CCW rotation)
#define SCREEN_W 416
#define SCREEN_H 336

// Colors
#define BG_COLOR        0x0000  // Black
#define TEXT_COLOR      0xFFFF  // White
#define MUTED_COLOR     0x8410  // Gray


BreathingExercise::BreathingExercise()
    : state(BreathingState::Disabled)
    , stateStartTime(0)
    , lastTriggerTime(0)
    , currentCycle(0)
    , previousState(BreathingState::Disabled)
    , pomodoroActive(false)
    , externalStateChange(false)
    , nadiSecondHalf(false)
    , enabled(false)
    , soundEnabled(true)  // Sound ON by default
    , startHour(DEFAULT_BREATHING_START_HOUR)
    , endHour(DEFAULT_BREATHING_END_HOUR)
    , intervalMinutes(DEFAULT_BREATHING_INTERVAL)
    , exerciseType(BreathingType::BoxBreathing) {
}

void BreathingExercise::begin() {
    loadSettings();
    if (enabled) {
        state = BreathingState::Idle;
    }
    Serial.println("[Breathing] Initialized");
    Serial.printf("[Breathing] Enabled: %s, Hours: %d-%d, Interval: %d min\n",
                  enabled ? "yes" : "no", startHour, endHour, intervalMinutes);
}

void BreathingExercise::loadSettings() {
    prefs.begin("breathing", true);  // Read-only
    enabled = prefs.getBool("enabled", false);
    soundEnabled = prefs.getBool("sound", true);  // Default ON
    startHour = prefs.getInt("startHour", DEFAULT_BREATHING_START_HOUR);
    endHour = prefs.getInt("endHour", DEFAULT_BREATHING_END_HOUR);
    intervalMinutes = prefs.getInt("interval", DEFAULT_BREATHING_INTERVAL);
    exerciseType = (BreathingType)prefs.getInt("exType", 0);
    prefs.end();
}

void BreathingExercise::saveSettings() {
    prefs.begin("breathing", false);  // Read-write
    prefs.putBool("enabled", enabled);
    prefs.putBool("sound", soundEnabled);
    prefs.putInt("startHour", startHour);
    prefs.putInt("endHour", endHour);
    prefs.putInt("interval", intervalMinutes);
    prefs.putInt("exType", (int)exerciseType);
    prefs.end();
}

void BreathingExercise::setEnabled(bool en) {
    enabled = en;
    if (enabled && state == BreathingState::Disabled) {
        state = BreathingState::Idle;
        lastTriggerTime = millis();  // Don't trigger immediately
    } else if (!enabled) {
        state = BreathingState::Disabled;
    }
    saveSettings();
    Serial.printf("[Breathing] Enabled: %s\n", enabled ? "yes" : "no");
}

void BreathingExercise::setSoundEnabled(bool en) {
    soundEnabled = en;
    saveSettings();
    Serial.printf("[Breathing] Sound: %s\n", soundEnabled ? "on" : "off");
}

void BreathingExercise::setTimeWindow(int start, int end) {
    startHour = constrain(start, 0, 23);
    endHour = constrain(end, 0, 23);
    saveSettings();
    Serial.printf("[Breathing] Time window: %d:00 - %d:00\n", startHour, endHour);
}

void BreathingExercise::setIntervalMinutes(int minutes) {
    intervalMinutes = constrain(minutes, 30, 180);
    saveSettings();
    Serial.printf("[Breathing] Interval: %d minutes\n", intervalMinutes);
}

void BreathingExercise::setExerciseType(BreathingType type) {
    exerciseType = type;
    saveSettings();
    Serial.printf("[Breathing] Type: %s\n",
                  type == BreathingType::NadiShodhana ? "Nadi Shodhana" : "Box Breathing");
}

uint32_t BreathingExercise::getStateDuration() const {
    switch (state) {
        case BreathingState::ShowingPrompt:
            return BREATHING_PROMPT_TIMEOUT_MS;
        case BreathingState::Confirmation:
            return BREATHING_CONFIRM_MS;
        case BreathingState::Inhale:
        case BreathingState::HoldIn:
        case BreathingState::Exhale:
        case BreathingState::HoldOut:
            return (exerciseType == BreathingType::NadiShodhana) ? NADI_PHASE_MS : BREATHING_PHASE_MS;
        case BreathingState::Complete:
            return BREATHING_COMPLETE_MS;
        default:
            return 0;
    }
}

bool BreathingExercise::shouldTrigger(int hour, int minute) {
    // Don't trigger during active pomodoro
    if (pomodoroActive) return false;

    // Check if within time window
    bool inWindow = false;
    if (startHour <= endHour) {
        // Normal range (e.g., 9-17)
        inWindow = (hour >= startHour && hour < endHour);
    } else {
        // Wraps midnight (e.g., 22-6)
        inWindow = (hour >= startHour || hour < endHour);
    }

    if (!inWindow) return false;

    // Check if enough time has passed since last trigger
    uint32_t intervalMs = (uint32_t)intervalMinutes * 60 * 1000;
    uint32_t now = millis();

    if (now - lastTriggerTime >= intervalMs) {
        return true;
    }

    return false;
}

void BreathingExercise::setState(BreathingState newState) {
    previousState = state;
    state = newState;
    stateStartTime = millis();

    const char* stateNames[] = {
        "Disabled", "Idle", "ShowingPrompt", "Confirmation", "Inhale", "HoldIn", "Exhale", "HoldOut", "Complete"
    };
    Serial.printf("[Breathing] State: %s\n", stateNames[(int)state]);
}

bool BreathingExercise::update(float dt, int currentHour, int currentMinute) {
    if (state == BreathingState::Disabled) {
        return false;
    }

    uint32_t now = millis();
    uint32_t elapsed = now - stateStartTime;
    uint32_t duration = getStateDuration();
    bool stateChanged = false;

    switch (state) {
        case BreathingState::Idle:
            // Check if we should trigger
            if (shouldTrigger(currentHour, currentMinute)) {
                lastTriggerTime = now;
                setState(BreathingState::ShowingPrompt);
                stateChanged = true;
            }
            break;

        case BreathingState::ShowingPrompt:
            // Timeout - user didn't respond
            if (elapsed >= duration) {
                setState(BreathingState::Idle);
                stateChanged = true;
            }
            break;

        case BreathingState::Confirmation:
            // "Let's Breathe" confirmation fades into Inhale
            if (elapsed >= duration) {
                setState(BreathingState::Inhale);
                stateChanged = true;
            }
            break;

        case BreathingState::Inhale:
            if (elapsed >= duration) {
                setState(BreathingState::HoldIn);
                stateChanged = true;
            }
            break;

        case BreathingState::HoldIn:
            if (elapsed >= duration) {
                setState(BreathingState::Exhale);
                stateChanged = true;
            }
            break;

        case BreathingState::Exhale:
            if (elapsed >= duration) {
                if (exerciseType == BreathingType::NadiShodhana) {
                    // Nadi Shodhana: 6 phases per cycle (no HoldOut)
                    if (nadiSecondHalf) {
                        // Finished right-to-left half → full cycle complete
                        currentCycle++;
                        nadiSecondHalf = false;
                        if (currentCycle >= NADI_CYCLES) {
                            setState(BreathingState::Complete);
                        } else {
                            setState(BreathingState::Inhale);
                        }
                    } else {
                        // Finished left-to-right half → start right-to-left
                        nadiSecondHalf = true;
                        setState(BreathingState::Inhale);
                    }
                } else {
                    setState(BreathingState::HoldOut);
                }
                stateChanged = true;
            }
            break;

        case BreathingState::HoldOut:
            if (elapsed >= duration) {
                currentCycle++;
                if (currentCycle >= BREATHING_CYCLES) {
                    setState(BreathingState::Complete);
                } else {
                    setState(BreathingState::Inhale);
                }
                stateChanged = true;
            }
            break;

        case BreathingState::Complete:
            if (elapsed >= duration) {
                setState(BreathingState::Idle);
                stateChanged = true;
            }
            break;

        default:
            break;
    }

    return stateChanged;
}

void BreathingExercise::start() {
    if (state == BreathingState::ShowingPrompt) {
        currentCycle = 0;
        nadiSecondHalf = false;
        setState(BreathingState::Confirmation);
        externalStateChange = true;  // Signal for sound trigger
        Serial.printf("[Breathing] Starting %s - showing confirmation\n",
                      exerciseType == BreathingType::NadiShodhana ? "Nadi Shodhana" : "Box Breathing");
    }
}

void BreathingExercise::skip() {
    if (state == BreathingState::ShowingPrompt) {
        setState(BreathingState::Idle);
        externalStateChange = true;  // Signal for sound trigger
        Serial.println("[Breathing] Skipped");
    }
}

void BreathingExercise::triggerNow() {
    // Manual trigger - doesn't affect scheduled timing
    if (state == BreathingState::Idle || state == BreathingState::Disabled) {
        // Don't modify lastTriggerTime - scheduled reminders continue at normal interval
        setState(BreathingState::ShowingPrompt);
        externalStateChange = true;  // Signal for sound trigger
        Serial.println("[Breathing] Triggered manually (no effect on scheduled timing)");
    }
}

bool BreathingExercise::consumeExternalStateChange() {
    bool changed = externalStateChange;
    externalStateChange = false;
    return changed;
}

bool BreathingExercise::isActive() const {
    return state == BreathingState::Inhale ||
           state == BreathingState::HoldIn ||
           state == BreathingState::Exhale ||
           state == BreathingState::HoldOut ||
           state == BreathingState::Complete;
}

bool BreathingExercise::needsFullScreenRender() const {
    return state == BreathingState::ShowingPrompt ||
           state == BreathingState::Confirmation ||
           isActive();
}

float BreathingExercise::getPhaseProgress() const {
    uint32_t duration = getStateDuration();
    if (duration == 0) return 0.0f;

    uint32_t elapsed = millis() - stateStartTime;
    return constrain((float)elapsed / duration, 0.0f, 1.0f);
}

float BreathingExercise::getPulseAlpha() const {
    // Slow sine wave pulsing (2 second cycle for mindful feel)
    float phase = (float)(millis() % 2000) / 2000.0f;
    return 0.5f + 0.5f * sinf(phase * 2.0f * M_PI);
}

void BreathingExercise::getTargetShape(EyeShape& out) const {
    float progress = getPhaseProgress();

    // Define key shapes
    EyeShape neutral;
    neutral.width = 1.0f;
    neutral.height = 1.0f;
    neutral.topLid = 0.0f;
    neutral.bottomLid = 0.0f;

    EyeShape inflated;
    inflated.width = 1.3f;
    inflated.height = 1.25f;
    inflated.topLid = 0.0f;
    inflated.bottomLid = 0.0f;
    inflated.cornerRadius = 1.4f;

    EyeShape deflated;
    deflated.width = 0.15f;
    deflated.height = 0.85f;
    deflated.topLid = 0.6f;
    deflated.bottomLid = 0.3f;
    deflated.cornerRadius = 1.5f;

    // Ease in-out function
    auto easeInOut = [](float t) -> float {
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    };

    switch (state) {
        case BreathingState::Inhale:
            // Lerp from deflated (or neutral on first cycle) to inflated
            if (currentCycle == 0 && progress < 0.5f) {
                // First half of first inhale: neutral to inflated
                out = EyeShape::lerp(neutral, inflated, easeInOut(progress * 2.0f));
            } else {
                // Deflated to inflated
                out = EyeShape::lerp(deflated, inflated, easeInOut(progress));
            }
            break;

        case BreathingState::HoldIn:
            out = inflated;
            break;

        case BreathingState::Exhale:
            out = EyeShape::lerp(inflated, deflated, easeInOut(progress));
            break;

        case BreathingState::HoldOut:
            out = deflated;
            break;

        case BreathingState::Complete:
            // Return to neutral
            out = EyeShape::lerp(deflated, neutral, easeInOut(progress));
            break;

        default:
            out = neutral;
            break;
    }
}

void BreathingExercise::getTargetShapes(EyeShape& leftOut, EyeShape& rightOut) const {
    if (exerciseType != BreathingType::NadiShodhana) {
        // Box breathing: both eyes identical
        getTargetShape(leftOut);
        rightOut = leftOut;
        return;
    }

    // Nadi Shodhana: asymmetric eyes
    float progress = getPhaseProgress();

    EyeShape neutral;
    EyeShape wideOpen;
    wideOpen.width = 1.3f;
    wideOpen.height = 1.25f;
    wideOpen.cornerRadius = 1.4f;

    EyeShape wink;  // Closed nostril side
    wink.width = 0.7f;
    wink.height = 0.3f;
    wink.topLid = 0.5f;
    wink.bottomLid = 0.4f;

    EyeShape holdShape;  // Both eyes during hold
    holdShape.width = 0.9f;
    holdShape.height = 1.0f;
    holdShape.topLid = 0.2f;

    auto easeInOut = [](float t) -> float {
        return t < 0.5f ? 4.0f * t * t * t : 1.0f - powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
    };

    // First half: inhale left, exhale right
    // Second half: inhale right, exhale left
    bool leftActive = !nadiSecondHalf;

    switch (state) {
        case BreathingState::Inhale: {
            EyeShape activeEye = EyeShape::lerp(holdShape, wideOpen, easeInOut(progress));
            if (currentCycle == 0 && !nadiSecondHalf && progress < 0.5f) {
                activeEye = EyeShape::lerp(neutral, wideOpen, easeInOut(progress * 2.0f));
            }
            EyeShape closedEye = wink;
            leftOut = leftActive ? activeEye : closedEye;
            rightOut = leftActive ? closedEye : activeEye;
            break;
        }
        case BreathingState::HoldIn:
            leftOut = holdShape;
            rightOut = holdShape;
            break;
        case BreathingState::Exhale: {
            // Exhale through OPPOSITE side
            bool exhaleLeftActive = !leftActive;
            EyeShape activeEye = EyeShape::lerp(wideOpen, holdShape, easeInOut(progress));
            EyeShape closedEye = wink;
            leftOut = exhaleLeftActive ? activeEye : closedEye;
            rightOut = exhaleLeftActive ? closedEye : activeEye;
            break;
        }
        case BreathingState::Complete:
            leftOut = EyeShape::lerp(holdShape, neutral, easeInOut(progress));
            rightOut = leftOut;
            break;
        default:
            leftOut = neutral;
            rightOut = neutral;
            break;
    }
}

// Rendering

void BreathingExercise::renderPromptScreen(uint16_t* buffer, int16_t bufW, int16_t bufH, uint16_t eyeColor) {
    TextRenderer::clearBuffer(buffer, bufW, bufH, BG_COLOR);

    // Pulsing "BREATHE" text (large, centered)
    float pulse = getPulseAlpha();
    uint16_t r = ((eyeColor >> 11) & 0x1F) * pulse;
    uint16_t g = ((eyeColor >> 5) & 0x3F) * pulse;
    uint16_t b = (eyeColor & 0x1F) * pulse;
    uint16_t pulsingColor = ((uint16_t)r << 11) | ((uint16_t)g << 5) | (uint16_t)b;

    // Draw "BREATHE" at scale 5 (big text)
    TextRenderer::drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 3 - 20, "BREATHE", pulsingColor, 5);

    // Divider line
    int16_t dividerY = SCREEN_H / 2 + 20;
    TextRenderer::drawFilledRect(buffer, bufW, bufH, 40, dividerY, SCREEN_W - 80, 2, MUTED_COLOR);

    // Button labels
    int16_t buttonY = dividerY + 40;

    // Left button: START
    TextRenderer::drawCenteredText(buffer, bufW, bufH, SCREEN_W / 4, buttonY, "START", eyeColor);

    // Right button: SKIP
    TextRenderer::drawCenteredText(buffer, bufW, bufH, 3 * SCREEN_W / 4, buttonY, "SKIP", MUTED_COLOR);

    // Cycle indicator at bottom
    if (exerciseType == BreathingType::NadiShodhana) {
        TextRenderer::drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 40, "3 CYCLES  72S", MUTED_COLOR);
    } else {
        TextRenderer::drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 40, "3 CYCLES  60S", MUTED_COLOR);
    }
}

void BreathingExercise::renderConfirmationScreen(uint16_t* buffer, int16_t bufW, int16_t bufH, uint16_t eyeColor) {
    TextRenderer::clearBuffer(buffer, bufW, bufH, BG_COLOR);

    float progress = getPhaseProgress();
    float alpha = 1.0f - progress;

    uint16_t r = ((eyeColor >> 11) & 0x1F) * alpha;
    uint16_t g = ((eyeColor >> 5) & 0x3F) * alpha;
    uint16_t b = (eyeColor & 0x1F) * alpha;
    uint16_t fadingColor = ((uint16_t)r << 11) | ((uint16_t)g << 5) | (uint16_t)b;

    TextRenderer::drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 - 30, "LETS", fadingColor, 5);
    TextRenderer::drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 30, "BREATHE", fadingColor, 5);
}

void BreathingExercise::renderPhaseText(uint16_t* buffer, int16_t bufW, int16_t bufH, uint16_t eyeColor) {
    if (state != BreathingState::Inhale && state != BreathingState::HoldIn &&
        state != BreathingState::Exhale && state != BreathingState::HoldOut) {
        return;
    }

    TextRenderer::clearBuffer(buffer, bufW, bufH, BG_COLOR);

    float progress = getPhaseProgress();
    float alpha;
    const char* text;

    const float MIN_ALPHA = 0.3f;
    const float MAX_ALPHA = 1.0f;

    if (exerciseType == BreathingType::NadiShodhana) {
        switch (state) {
            case BreathingState::Inhale:
                text = nadiSecondHalf ? "IN R" : "IN L";
                alpha = MIN_ALPHA + progress * (MAX_ALPHA - MIN_ALPHA);
                break;
            case BreathingState::HoldIn:
                text = "HOLD";
                alpha = MAX_ALPHA;
                break;
            case BreathingState::Exhale:
                text = nadiSecondHalf ? "OUT L" : "OUT R";
                alpha = MAX_ALPHA - progress * (MAX_ALPHA - MIN_ALPHA);
                break;
            default:
                return;
        }
    } else {
        switch (state) {
            case BreathingState::Inhale:
                text = "IN";
                alpha = MIN_ALPHA + progress * (MAX_ALPHA - MIN_ALPHA);
                break;
            case BreathingState::HoldIn:
                text = "HOLD";
                alpha = MAX_ALPHA;
                break;
            case BreathingState::Exhale:
                text = "OUT";
                alpha = MAX_ALPHA - progress * (MAX_ALPHA - MIN_ALPHA);
                break;
            case BreathingState::HoldOut:
                text = "HOLD";
                alpha = MIN_ALPHA;
                break;
            default:
                return;
        }
    }

    uint16_t r = ((eyeColor >> 11) & 0x1F) * alpha;
    uint16_t g = ((eyeColor >> 5) & 0x3F) * alpha;
    uint16_t b = (eyeColor & 0x1F) * alpha;
    uint16_t fadedColor = ((uint16_t)r << 11) | ((uint16_t)g << 5) | (uint16_t)b;

    TextRenderer::drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 - 21, text, fadedColor, 6);
}

void BreathingExercise::renderPhaseTextOverlay(uint16_t* buffer, int16_t bufW, int16_t bufH, uint16_t eyeColor) {
    if (state != BreathingState::Inhale && state != BreathingState::HoldIn &&
        state != BreathingState::Exhale) {
        return;
    }

    float progress = getPhaseProgress();
    float alpha;
    const char* text;

    const float MIN_ALPHA = 0.3f;
    const float MAX_ALPHA = 1.0f;

    switch (state) {
        case BreathingState::Inhale:
            text = nadiSecondHalf ? "IN R" : "IN L";
            alpha = MIN_ALPHA + progress * (MAX_ALPHA - MIN_ALPHA);
            break;
        case BreathingState::HoldIn:
            text = "HOLD";
            alpha = MAX_ALPHA;
            break;
        case BreathingState::Exhale:
            text = nadiSecondHalf ? "OUT L" : "OUT R";
            alpha = MAX_ALPHA - progress * (MAX_ALPHA - MIN_ALPHA);
            break;
        default:
            return;
    }

    uint16_t r = ((eyeColor >> 11) & 0x1F) * alpha;
    uint16_t g = ((eyeColor >> 5) & 0x3F) * alpha;
    uint16_t b = (eyeColor & 0x1F) * alpha;
    uint16_t fadedColor = ((uint16_t)r << 11) | ((uint16_t)g << 5) | (uint16_t)b;

    TextRenderer::drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H * 3 / 4, text, fadedColor, 4);
}
