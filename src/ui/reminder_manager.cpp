/**
 * @file reminder_manager.cpp
 * @brief Timed reminder system implementation
 */

#include "reminder_manager.h"
#include "text_renderer.h"
#include <ArduinoJson.h>
#include <algorithm>

// Screen dimensions (after 90° CCW rotation)
#define SCREEN_W 416
#define SCREEN_H 336

#define BG_COLOR    0x0000  // Black
#define MUTED_COLOR 0x8410  // Gray


ReminderManager::ReminderManager()
    : state(ReminderState::Idle)
    , activeIndex(-1)
    , showStartTime(0)
    , snoozeUntil(0)
    , snoozedIndex(-1)
    , lastTriggeredMinute(-1)
    , lastTriggeredHour(-1)
    , autoSnoozeCount(0)
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
    sortByTime();
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

void ReminderManager::sortByTime() {
    // Remember which reminders are referenced by index
    Reminder activeSnapshot, snoozedSnapshot;
    bool hasActive = (activeIndex >= 0 && activeIndex < (int)reminders.size());
    bool hasSnoozed = (snoozedIndex >= 0 && snoozedIndex < (int)reminders.size());
    if (hasActive) activeSnapshot = reminders[activeIndex];
    if (hasSnoozed) snoozedSnapshot = reminders[snoozedIndex];

    std::sort(reminders.begin(), reminders.end(), [](const Reminder& a, const Reminder& b) {
        if (a.hour != b.hour) return a.hour < b.hour;
        return a.minute < b.minute;
    });

    // Restore indices by matching hour+minute+message
    if (hasActive) {
        for (int i = 0; i < (int)reminders.size(); i++) {
            if (reminders[i].hour == activeSnapshot.hour &&
                reminders[i].minute == activeSnapshot.minute &&
                strcmp(reminders[i].message, activeSnapshot.message) == 0) {
                activeIndex = i;
                break;
            }
        }
    }
    if (hasSnoozed) {
        for (int i = 0; i < (int)reminders.size(); i++) {
            if (reminders[i].hour == snoozedSnapshot.hour &&
                reminders[i].minute == snoozedSnapshot.minute &&
                strcmp(reminders[i].message, snoozedSnapshot.message) == 0) {
                snoozedIndex = i;
                break;
            }
        }
    }
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
    sortByTime();
    saveToNVS();
    Serial.printf("[Reminder] Added: %02d:%02d \"%s\" %s\n",
                  r.hour, r.minute, r.message, r.recurring ? "(recurring)" : "");
    return true;
}

bool ReminderManager::edit(int index, uint8_t hour, uint8_t minute, const char* message, bool recurring) {
    if (index < 0 || index >= (int)reminders.size()) return false;
    if (!message || strlen(message) == 0) return false;

    Reminder& r = reminders[index];
    r.hour = hour % 24;
    r.minute = minute % 60;
    r.recurring = recurring;
    strncpy(r.message, message, REMINDER_MAX_MESSAGE);
    r.message[REMINDER_MAX_MESSAGE] = '\0';

    // Convert to uppercase for display
    for (int i = 0; r.message[i]; i++) {
        if (r.message[i] >= 'a' && r.message[i] <= 'z') {
            r.message[i] -= 32;
        }
    }

    sortByTime();
    saveToNVS();
    Serial.printf("[Reminder] Edited: %02d:%02d \"%s\" %s\n",
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
    autoSnoozeCount = 0;
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

bool ReminderManager::update(float dt, int currentHour, int currentMinute, bool timeValid) {
    if (reminders.empty() && snoozeUntil == 0) return false;

    uint32_t now = millis();
    bool stateChanged = false;

    // Check snooze timer (millis-based, works regardless of time validity)
    if (snoozeUntil > 0 && now >= snoozeUntil && state == ReminderState::Idle) {
        if (snoozedIndex >= 0 && snoozedIndex < (int)reminders.size() && !isBlocked) {
            activeIndex = snoozedIndex;
            state = ReminderState::Showing;
            showStartTime = now;
            snoozeUntil = 0;
            snoozedIndex = -1;
            stateChanged = true;
            Serial.printf("[Reminder] Snooze triggered: \"%s\" (time now: %02d:%02d, ntp: %s, auto-snooze: %d/%d)\n",
                          reminders[activeIndex].message, currentHour, currentMinute,
                          timeValid ? "yes" : "NO", autoSnoozeCount, REMINDER_MAX_AUTO_SNOOZE);
        }
    }

    // Auto-snooze: if showing for too long with no interaction
    if (state == ReminderState::Showing) {
        if (now - showStartTime >= REMINDER_AUTO_SNOOZE_MS) {
            autoSnoozeCount++;
            if (autoSnoozeCount >= REMINDER_MAX_AUTO_SNOOZE) {
                Serial.printf("[Reminder] Auto-dismiss after %d auto-snoozes\n", autoSnoozeCount);
                dismiss();
            } else {
                Serial.printf("[Reminder] Auto-snooze %d/%d (no interaction)\n",
                              autoSnoozeCount, REMINDER_MAX_AUTO_SNOOZE);
                snooze();
            }
            return true;
        }
        return false;  // Don't check new triggers while showing
    }

    // Don't trigger during other full-screen activities
    if (isBlocked) return false;

    // Only match reminders against reliable (NTP-synced) time
    if (!timeValid) return stateChanged;

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
            autoSnoozeCount = 0;  // Fresh trigger resets auto-snooze count
            stateChanged = true;
            Serial.printf("[Reminder] TRIGGERED: %02d:%02d \"%s\" (current NTP time: %02d:%02d)\n",
                          reminders[i].hour, reminders[i].minute, reminders[i].message,
                          currentHour, currentMinute);
            break;  // Only show one at a time
        }
    }

    return stateChanged;
}

void ReminderManager::renderPrompt(uint16_t* buffer, int16_t bufW, int16_t bufH, uint16_t eyeColor) {
    TextRenderer::clearBuffer(buffer, bufW, bufH, BG_COLOR);

    if (activeIndex < 0 || activeIndex >= (int)reminders.size()) return;

    const Reminder& r = reminders[activeIndex];

    // Time display at top: "14:00" in muted color
    char timeStr[8];
    sprintf(timeStr, "%02d:%02d", r.hour, r.minute);
    TextRenderer::drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, 40, timeStr, MUTED_COLOR, 4);

    // Reminder message (large, centered, word-wrapped)
    int msgLen = strlen(r.message);
    int scale = (msgLen <= 24) ? 5 : 4;
    int maxChars = (scale == 5) ? 12 : 15;

    int16_t msgStartY = SCREEN_H / 2 - 40;
    TextRenderer::drawWrappedText(buffer, bufW, bufH, SCREEN_W / 2, msgStartY, r.message,
                                  eyeColor, scale, maxChars);

    // Divider line
    int16_t dividerY = SCREEN_H - 80;
    TextRenderer::drawFilledRect(buffer, bufW, bufH, 40, dividerY, SCREEN_W - 80, 2, MUTED_COLOR);

    // Button labels
    int16_t buttonY = dividerY + 30;

    // Left: SNOOZE
    TextRenderer::drawCenteredText(buffer, bufW, bufH, SCREEN_W / 4, buttonY, "SNOOZE", MUTED_COLOR, 3);

    // Right: OK
    TextRenderer::drawCenteredText(buffer, bufW, bufH, 3 * SCREEN_W / 4, buttonY, "OK", eyeColor, 3);
}
