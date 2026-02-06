/**
 * @file reminder_manager.cpp
 * @brief Timed reminder system implementation
 */

#include "reminder_manager.h"
#include <ArduinoJson.h>

// Screen dimensions (after 90° CCW rotation)
#define SCREEN_W 416
#define SCREEN_H 336

#define BG_COLOR    0x0000  // Black
#define MUTED_COLOR 0x8410  // Gray

// 5x7 bitmap font (same as breathing_exercise.cpp / settings_menu.cpp)
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
    {0x00, 0x36, 0x36, 0x00, 0x00}, // : (colon, index 37)
    {0x00, 0x00, 0x40, 0x00, 0x00}, // . (period, index 38)
    {0x08, 0x08, 0x08, 0x08, 0x08}, // - (dash, index 39)
    {0x20, 0x10, 0x08, 0x04, 0x02}, // / (slash, index 40)
    {0x00, 0x60, 0x60, 0x00, 0x00}, // ' (apostrophe, index 41)
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ? (question mark, index 42)
    {0x00, 0x00, 0x4F, 0x00, 0x00}, // ! (exclamation, index 43)
};

ReminderManager::ReminderManager()
    : state(ReminderState::Idle)
    , activeIndex(-1)
    , showStartTime(0)
    , snoozeUntil(0)
    , snoozedIndex(-1)
    , lastTriggeredMinute(-1)
    , lastTriggeredHour(-1)
    , externalStateChange(false)
    , isBlocked(false)
{
}

void ReminderManager::begin() {
    loadFromNVS();
    Serial.printf("[Reminder] Loaded %d reminders\n", reminders.size());
}

void ReminderManager::loadFromNVS() {
    prefs.begin("reminders", true);
    String data = prefs.getString("data", "[]");
    prefs.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data);
    if (err) {
        Serial.printf("[Reminder] JSON parse error: %s\n", err.c_str());
        return;
    }

    reminders.clear();
    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
        if (reminders.size() >= REMINDER_MAX_COUNT) break;
        Reminder r;
        r.hour = obj["h"] | 0;
        r.minute = obj["m"] | 0;
        r.recurring = obj["r"] | false;
        r.enabled = true;
        const char* msg = obj["msg"] | "";
        strncpy(r.message, msg, REMINDER_MAX_MESSAGE);
        r.message[REMINDER_MAX_MESSAGE] = '\0';
        reminders.push_back(r);
    }
}

void ReminderManager::saveToNVS() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& r : reminders) {
        JsonObject obj = arr.add<JsonObject>();
        obj["h"] = r.hour;
        obj["m"] = r.minute;
        obj["msg"] = r.message;
        if (r.recurring) obj["r"] = true;
    }

    String data;
    serializeJson(doc, data);

    prefs.begin("reminders", false);
    prefs.putString("data", data);
    prefs.end();

    Serial.printf("[Reminder] Saved %d reminders (%d bytes)\n", reminders.size(), data.length());
}

bool ReminderManager::add(uint8_t hour, uint8_t minute, const char* message, bool recurring) {
    if (reminders.size() >= REMINDER_MAX_COUNT) return false;
    if (!message || strlen(message) == 0) return false;

    Reminder r;
    r.hour = hour % 24;
    r.minute = minute % 60;
    r.recurring = recurring;
    r.enabled = true;
    strncpy(r.message, message, REMINDER_MAX_MESSAGE);
    r.message[REMINDER_MAX_MESSAGE] = '\0';

    // Convert to uppercase for display
    for (int i = 0; r.message[i]; i++) {
        if (r.message[i] >= 'a' && r.message[i] <= 'z') {
            r.message[i] -= 32;
        }
    }

    reminders.push_back(r);
    saveToNVS();
    Serial.printf("[Reminder] Added: %02d:%02d \"%s\" %s\n",
                  r.hour, r.minute, r.message, r.recurring ? "(recurring)" : "");
    return true;
}

void ReminderManager::remove(int index) {
    if (index < 0 || index >= (int)reminders.size()) return;

    Serial.printf("[Reminder] Removed: \"%s\"\n", reminders[index].message);
    reminders.erase(reminders.begin() + index);
    saveToNVS();

    // If we removed the active reminder, return to idle
    if (state == ReminderState::Showing && activeIndex == index) {
        state = ReminderState::Idle;
        activeIndex = -1;
        externalStateChange = true;
    }
}

bool ReminderManager::removeByMessage(const char* substring) {
    if (!substring) return false;

    // Convert search term to uppercase for comparison
    String search = substring;
    search.toUpperCase();

    for (int i = 0; i < (int)reminders.size(); i++) {
        String msg = reminders[i].message;
        if (msg.indexOf(search) >= 0) {
            remove(i);
            return true;
        }
    }
    return false;
}

void ReminderManager::dismiss() {
    if (state != ReminderState::Showing) return;

    Serial.printf("[Reminder] Dismissed: \"%s\"\n", reminders[activeIndex].message);

    if (!reminders[activeIndex].recurring) {
        // One-shot: remove it
        reminders.erase(reminders.begin() + activeIndex);
        saveToNVS();
    }

    state = ReminderState::Idle;
    activeIndex = -1;
    snoozeUntil = 0;
    snoozedIndex = -1;
    externalStateChange = true;
}

void ReminderManager::snooze() {
    if (state != ReminderState::Showing) return;

    Serial.printf("[Reminder] Snoozed: \"%s\" (5 min)\n", reminders[activeIndex].message);
    snoozedIndex = activeIndex;
    snoozeUntil = millis() + REMINDER_SNOOZE_MS;
    state = ReminderState::Idle;
    activeIndex = -1;
    externalStateChange = true;
}

bool ReminderManager::consumeExternalStateChange() {
    bool changed = externalStateChange;
    externalStateChange = false;
    return changed;
}

const Reminder* ReminderManager::getActiveReminder() const {
    if (state != ReminderState::Showing || activeIndex < 0 ||
        activeIndex >= (int)reminders.size()) {
        return nullptr;
    }
    return &reminders[activeIndex];
}

bool ReminderManager::update(float dt, int currentHour, int currentMinute) {
    if (reminders.empty() && snoozeUntil == 0) return false;

    uint32_t now = millis();
    bool stateChanged = false;

    // Check snooze timer
    if (snoozeUntil > 0 && now >= snoozeUntil && state == ReminderState::Idle) {
        if (snoozedIndex >= 0 && snoozedIndex < (int)reminders.size() && !isBlocked) {
            activeIndex = snoozedIndex;
            state = ReminderState::Showing;
            showStartTime = now;
            snoozeUntil = 0;
            snoozedIndex = -1;
            stateChanged = true;
            Serial.printf("[Reminder] Snooze triggered: \"%s\"\n", reminders[activeIndex].message);
        }
    }

    // Auto-snooze: if showing for too long with no interaction
    if (state == ReminderState::Showing) {
        if (now - showStartTime >= REMINDER_AUTO_SNOOZE_MS) {
            Serial.println("[Reminder] Auto-snooze (no interaction)");
            snooze();
            return true;
        }
        return false;  // Don't check new triggers while showing
    }

    // Don't trigger during other full-screen activities
    if (isBlocked) return false;

    // Check time-based triggers (once per minute change)
    if (currentHour == lastTriggeredHour && currentMinute == lastTriggeredMinute) {
        return stateChanged;
    }
    lastTriggeredHour = currentHour;
    lastTriggeredMinute = currentMinute;

    for (int i = 0; i < (int)reminders.size(); i++) {
        if (!reminders[i].enabled) continue;
        if (reminders[i].hour == currentHour && reminders[i].minute == currentMinute) {
            // Don't re-trigger a snoozed reminder by time match
            if (snoozedIndex == i) continue;

            activeIndex = i;
            state = ReminderState::Showing;
            showStartTime = now;
            stateChanged = true;
            Serial.printf("[Reminder] Triggered: %02d:%02d \"%s\"\n",
                          reminders[i].hour, reminders[i].minute, reminders[i].message);
            break;  // Only show one at a time
        }
    }

    return stateChanged;
}

// ============================================================================
// Rendering
// ============================================================================

void ReminderManager::drawFilledRect(uint16_t* buffer, int16_t bufW, int16_t bufH,
                                      int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
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

void ReminderManager::drawChar(uint16_t* buffer, int16_t bufW, int16_t bufH,
                                int16_t x, int16_t y, char c, uint16_t color, int scale) {
    int fontIdx = -1;

    if (c >= '0' && c <= '9') {
        fontIdx = c - '0';
    } else if (c == ' ') {
        fontIdx = 10;
    } else if (c >= 'A' && c <= 'Z') {
        fontIdx = 11 + (c - 'A');
    } else if (c >= 'a' && c <= 'z') {
        fontIdx = 11 + (c - 'a');
    } else if (c == ':') {
        fontIdx = 37;
    } else if (c == '.') {
        fontIdx = 38;
    } else if (c == '-') {
        fontIdx = 39;
    } else if (c == '/') {
        fontIdx = 40;
    } else if (c == '\'') {
        fontIdx = 41;
    } else if (c == '?') {
        fontIdx = 42;
    } else if (c == '!') {
        fontIdx = 43;
    }

    if (fontIdx < 0 || fontIdx >= 44) return;

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

void ReminderManager::drawText(uint16_t* buffer, int16_t bufW, int16_t bufH,
                                int16_t x, int16_t y, const char* text, uint16_t color, int scale) {
    int charWidth = 5 * scale + scale;  // char pixels + spacing
    int16_t curX = x;
    while (*text) {
        drawChar(buffer, bufW, bufH, curX, y, *text, color, scale);
        curX += charWidth;
        text++;
    }
}

void ReminderManager::drawCenteredText(uint16_t* buffer, int16_t bufW, int16_t bufH,
                                        int16_t centerX, int16_t y, const char* text,
                                        uint16_t color, int scale) {
    int len = strlen(text);
    int charWidth = 5 * scale + scale;
    int16_t totalWidth = len * charWidth;
    int16_t x = centerX - totalWidth / 2;
    drawText(buffer, bufW, bufH, x, y, text, color, scale);
}

void ReminderManager::drawWrappedText(uint16_t* buffer, int16_t bufW, int16_t bufH,
                                       int16_t centerX, int16_t startY, const char* text,
                                       uint16_t color, int scale, int maxCharsPerLine) {
    int len = strlen(text);
    int charWidth = 5 * scale + scale;
    int lineHeight = 7 * scale + scale * 2;  // char height + spacing

    // Split into lines at word boundaries
    int lineStart = 0;
    int lineCount = 0;
    char lineBuf[REMINDER_MAX_MESSAGE + 1];

    while (lineStart < len && lineCount < 4) {
        int lineEnd = lineStart + maxCharsPerLine;
        if (lineEnd >= len) {
            lineEnd = len;
        } else {
            // Find last space before limit for word wrap
            int lastSpace = lineEnd;
            while (lastSpace > lineStart && text[lastSpace] != ' ') {
                lastSpace--;
            }
            if (lastSpace > lineStart) {
                lineEnd = lastSpace;
            }
        }

        int lineLen = lineEnd - lineStart;
        strncpy(lineBuf, text + lineStart, lineLen);
        lineBuf[lineLen] = '\0';

        // Trim leading space
        const char* trimmed = lineBuf;
        while (*trimmed == ' ') trimmed++;

        int16_t y = startY + lineCount * lineHeight;
        drawCenteredText(buffer, bufW, bufH, centerX, y, trimmed, color, scale);

        lineStart = lineEnd;
        // Skip the space at the break
        if (lineStart < len && text[lineStart] == ' ') lineStart++;
        lineCount++;
    }
}

void ReminderManager::renderPrompt(uint16_t* buffer, int16_t bufW, int16_t bufH, uint16_t eyeColor) {
    // Clear to black
    for (int i = 0; i < bufW * bufH; i++) {
        buffer[i] = BG_COLOR;
    }

    if (activeIndex < 0 || activeIndex >= (int)reminders.size()) return;

    const Reminder& r = reminders[activeIndex];

    // Time display at top: "14:00" in muted color
    char timeStr[8];
    sprintf(timeStr, "%02d:%02d", r.hour, r.minute);
    drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, 40, timeStr, MUTED_COLOR, 4);

    // Reminder message (large, centered, word-wrapped)
    // Scale 5: char = 30x35px, ~12 chars per line fits in 416px
    // Scale 4: char = 24x28px, ~15 chars per line
    int msgLen = strlen(r.message);
    int scale = (msgLen <= 24) ? 5 : 4;
    int maxChars = (scale == 5) ? 12 : 15;

    int16_t msgStartY = SCREEN_H / 2 - 40;
    drawWrappedText(buffer, bufW, bufH, SCREEN_W / 2, msgStartY, r.message,
                    eyeColor, scale, maxChars);

    // Divider line
    int16_t dividerY = SCREEN_H - 80;
    drawFilledRect(buffer, bufW, bufH, 40, dividerY, SCREEN_W - 80, 2, MUTED_COLOR);

    // Button labels
    int16_t buttonY = dividerY + 30;

    // Left: SNOOZE
    drawCenteredText(buffer, bufW, bufH, SCREEN_W / 4, buttonY, "SNOOZE", MUTED_COLOR, 3);

    // Right: OK
    drawCenteredText(buffer, bufW, bufH, 3 * SCREEN_W / 4, buttonY, "OK", eyeColor, 3);
}
