/**
 * @file breathing_exercise.cpp
 * @brief Mindfulness breathing exercise implementation
 */

#include "breathing_exercise.h"
#include <math.h>

// Screen dimensions (after 90° CCW rotation)
#define SCREEN_W 416
#define SCREEN_H 336

// Colors
#define BG_COLOR        0x0000  // Black
#define TEXT_COLOR      0xFFFF  // White
#define MUTED_COLOR     0x8410  // Gray

// Simple 5x7 font data (same as settings_menu.cpp)
static const uint8_t FONT_5X7[][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x00, 0x00, 0x00, 0x00}, // (space, index 10)
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A (index 11)
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z (index 36)
};

BreathingExercise::BreathingExercise()
    : state(BreathingState::Disabled)
    , stateStartTime(0)
    , lastTriggerTime(0)
    , currentCycle(0)
    , previousState(BreathingState::Disabled)
    , pomodoroActive(false)
    , externalStateChange(false)
    , enabled(false)
    , soundEnabled(true)  // Sound ON by default
    , startHour(DEFAULT_BREATHING_START_HOUR)
    , endHour(DEFAULT_BREATHING_END_HOUR)
    , intervalMinutes(DEFAULT_BREATHING_INTERVAL) {
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
    prefs.end();
}

void BreathingExercise::saveSettings() {
    prefs.begin("breathing", false);  // Read-write
    prefs.putBool("enabled", enabled);
    prefs.putBool("sound", soundEnabled);
    prefs.putInt("startHour", startHour);
    prefs.putInt("endHour", endHour);
    prefs.putInt("interval", intervalMinutes);
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
            return BREATHING_PHASE_MS;
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
                setState(BreathingState::HoldOut);
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
        setState(BreathingState::Confirmation);
        externalStateChange = true;  // Signal for sound trigger
        Serial.println("[Breathing] Starting exercise - showing confirmation");
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

// Rendering helpers

void BreathingExercise::drawFilledRect(uint16_t* buffer, int16_t bufW, int16_t bufH,
                                        int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    // 90° CCW rotation: screen (sx, sy) → buffer (sy, bufH - 1 - sx)
    for (int16_t sy = y; sy < y + h; sy++) {
        for (int16_t sx = x; sx < x + w; sx++) {
            int16_t bx = sy;
            int16_t by = bufH - 1 - sx;
            if (bx >= 0 && bx < bufW && by >= 0 && by < bufH) {
                buffer[by * bufW + bx] = color;
            }
        }
    }
}

void BreathingExercise::drawChar(uint16_t* buffer, int16_t bufW, int16_t bufH,
                                  int16_t x, int16_t y, char c, uint16_t color) {
    int fontIdx = -1;

    if (c >= '0' && c <= '9') {
        fontIdx = c - '0';
    } else if (c == ' ') {
        fontIdx = 10;
    } else if (c >= 'A' && c <= 'Z') {
        fontIdx = 11 + (c - 'A');
    } else if (c >= 'a' && c <= 'z') {
        fontIdx = 11 + (c - 'a');
    }

    if (fontIdx < 0 || fontIdx >= 37) return;

    const uint8_t* charData = FONT_5X7[fontIdx];
    // 3x scaling with 90° CCW rotation
    for (int col = 0; col < 5; col++) {
        uint8_t colBits = charData[col];
        for (int row = 0; row < 7; row++) {
            if (colBits & (1 << row)) {
                for (int sy = 0; sy < 3; sy++) {
                    for (int sx = 0; sx < 3; sx++) {
                        int16_t screenX = x + col * 3 + sx;
                        int16_t screenY = y + row * 3 + sy;
                        // 90° CCW: buffer (sy, bufH - 1 - sx)
                        int16_t bx = screenY;
                        int16_t by = bufH - 1 - screenX;
                        if (bx >= 0 && bx < bufW && by >= 0 && by < bufH) {
                            buffer[by * bufW + bx] = color;
                        }
                    }
                }
            }
        }
    }
}

void BreathingExercise::drawText(uint16_t* buffer, int16_t bufW, int16_t bufH,
                                  int16_t x, int16_t y, const char* text, uint16_t color) {
    int16_t curX = x;
    while (*text) {
        drawChar(buffer, bufW, bufH, curX, y, *text, color);
        curX += 18;
        text++;
    }
}

void BreathingExercise::drawCenteredText(uint16_t* buffer, int16_t bufW, int16_t bufH,
                                          int16_t centerX, int16_t y, const char* text, uint16_t color) {
    int len = strlen(text);
    int16_t textWidth = len * 18;
    int16_t x = centerX - textWidth / 2;
    drawText(buffer, bufW, bufH, x, y, text, color);
}

void BreathingExercise::drawLargeText(uint16_t* buffer, int16_t bufW, int16_t bufH,
                                       int16_t centerX, int16_t y, const char* text, uint16_t color, int scale) {
    int len = strlen(text);
    int charWidth = 5 * scale + scale;  // 5 pixels per char + spacing
    int16_t totalWidth = len * charWidth;
    int16_t x = centerX - totalWidth / 2;

    while (*text) {
        char c = *text;
        int fontIdx = -1;

        if (c >= '0' && c <= '9') {
            fontIdx = c - '0';
        } else if (c == ' ') {
            fontIdx = 10;
        } else if (c >= 'A' && c <= 'Z') {
            fontIdx = 11 + (c - 'A');
        } else if (c >= 'a' && c <= 'z') {
            fontIdx = 11 + (c - 'a');
        }

        if (fontIdx >= 0 && fontIdx < 37) {
            const uint8_t* charData = FONT_5X7[fontIdx];
            for (int col = 0; col < 5; col++) {
                uint8_t colBits = charData[col];
                for (int row = 0; row < 7; row++) {
                    if (colBits & (1 << row)) {
                        for (int sy = 0; sy < scale; sy++) {
                            for (int sx = 0; sx < scale; sx++) {
                                int16_t screenX = x + col * scale + sx;
                                int16_t screenY = y + row * scale + sy;
                                int16_t bx = screenY;
                                int16_t by = bufH - 1 - screenX;
                                if (bx >= 0 && bx < bufW && by >= 0 && by < bufH) {
                                    buffer[by * bufW + bx] = color;
                                }
                            }
                        }
                    }
                }
            }
        }

        x += charWidth;
        text++;
    }
}

void BreathingExercise::renderPromptScreen(uint16_t* buffer, int16_t bufW, int16_t bufH, uint16_t eyeColor) {
    // Clear to black
    for (int i = 0; i < bufW * bufH; i++) {
        buffer[i] = BG_COLOR;
    }

    // Pulsing "BREATHE" text (large, centered)
    float pulse = getPulseAlpha();
    // Interpolate color brightness based on pulse
    uint16_t r = ((eyeColor >> 11) & 0x1F) * pulse;
    uint16_t g = ((eyeColor >> 5) & 0x3F) * pulse;
    uint16_t b = (eyeColor & 0x1F) * pulse;
    uint16_t pulsingColor = ((uint16_t)r << 11) | ((uint16_t)g << 5) | (uint16_t)b;

    // Draw "BREATHE" at scale 5 (big text)
    drawLargeText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 3 - 20, "BREATHE", pulsingColor, 5);

    // Divider line
    int16_t dividerY = SCREEN_H / 2 + 20;
    drawFilledRect(buffer, bufW, bufH, 40, dividerY, SCREEN_W - 80, 2, MUTED_COLOR);

    // Button labels
    int16_t buttonY = dividerY + 40;

    // Left button: START
    drawCenteredText(buffer, bufW, bufH, SCREEN_W / 4, buttonY, "START", eyeColor);

    // Right button: SKIP
    drawCenteredText(buffer, bufW, bufH, 3 * SCREEN_W / 4, buttonY, "SKIP", MUTED_COLOR);

    // Cycle indicator at bottom
    char cycleStr[16];
    sprintf(cycleStr, "3 CYCLES  60S");
    drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 40, cycleStr, MUTED_COLOR);
}

void BreathingExercise::renderConfirmationScreen(uint16_t* buffer, int16_t bufW, int16_t bufH, uint16_t eyeColor) {
    // Clear to black
    for (int i = 0; i < bufW * bufH; i++) {
        buffer[i] = BG_COLOR;
    }

    // Calculate fade-out alpha based on progress (1.0 -> 0.0 over duration)
    float progress = getPhaseProgress();
    float alpha = 1.0f - progress;  // Fades out as we approach Inhale

    // Interpolate color brightness based on alpha
    uint16_t r = ((eyeColor >> 11) & 0x1F) * alpha;
    uint16_t g = ((eyeColor >> 5) & 0x3F) * alpha;
    uint16_t b = (eyeColor & 0x1F) * alpha;
    uint16_t fadingColor = ((uint16_t)r << 11) | ((uint16_t)g << 5) | (uint16_t)b;

    // Draw "LET'S BREATHE" centered (large text, fading out)
    drawLargeText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 - 30, "LETS", fadingColor, 5);
    drawLargeText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 30, "BREATHE", fadingColor, 5);
}

void BreathingExercise::renderPhaseText(uint16_t* buffer, int16_t bufW, int16_t bufH, uint16_t eyeColor) {
    // Only render during active breathing phases
    if (state != BreathingState::Inhale && state != BreathingState::HoldIn &&
        state != BreathingState::Exhale && state != BreathingState::HoldOut) {
        return;
    }

    // Clear to black - full screen takeover
    for (int i = 0; i < bufW * bufH; i++) {
        buffer[i] = BG_COLOR;
    }

    float progress = getPhaseProgress();  // 0.0 to 1.0
    float alpha;
    const char* text;

    // Opacity range: 0.3 to 1.0 (never fully transparent for continuity)
    const float MIN_ALPHA = 0.3f;
    const float MAX_ALPHA = 1.0f;

    switch (state) {
        case BreathingState::Inhale:
            text = "IN";
            alpha = MIN_ALPHA + progress * (MAX_ALPHA - MIN_ALPHA);  // 0.3 → 1.0
            break;
        case BreathingState::HoldIn:
            text = "HOLD";
            alpha = MAX_ALPHA;  // Constant 1.0
            break;
        case BreathingState::Exhale:
            text = "OUT";
            alpha = MAX_ALPHA - progress * (MAX_ALPHA - MIN_ALPHA);  // 1.0 → 0.3
            break;
        case BreathingState::HoldOut:
            text = "HOLD";
            alpha = MIN_ALPHA;  // Constant 0.3
            break;
        default:
            return;
    }

    // Apply alpha to eye color (RGB565)
    uint16_t r = ((eyeColor >> 11) & 0x1F) * alpha;
    uint16_t g = ((eyeColor >> 5) & 0x3F) * alpha;
    uint16_t b = (eyeColor & 0x1F) * alpha;
    uint16_t fadedColor = ((uint16_t)r << 11) | ((uint16_t)g << 5) | (uint16_t)b;

    // Draw large centered text (scale 6 for prominent display)
    // Screen dimensions after rotation: SCREEN_W=416, SCREEN_H=336
    // Center at SCREEN_W/2 = 208 horizontal, SCREEN_H/2 = 168 vertical
    drawLargeText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 - 21, text, fadedColor, 6);
}
