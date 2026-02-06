/**
 * @file reminder_manager.h
 * @brief Timed reminder system with NVS persistence
 *
 * Supports up to 20 reminders, each with:
 * - Trigger time (hour:minute)
 * - Message (up to 48 chars, shown on screen)
 * - One-shot or recurring (daily)
 *
 * When triggered: alert sound + full-screen message
 * Interaction: left half = snooze (5 min), right half = dismiss
 * Auto-snooze after 60 seconds of no interaction
 */

#ifndef REMINDER_MANAGER_H
#define REMINDER_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
#include <vector>

#define REMINDER_MAX_MESSAGE    48
#define REMINDER_MAX_COUNT      20
#define REMINDER_SNOOZE_MS      300000  // 5 minutes
#define REMINDER_AUTO_SNOOZE_MS 60000   // 60 seconds before auto-snooze

enum class ReminderState {
    Idle,       // Waiting for a reminder to match current time
    Showing     // Displaying reminder on screen, waiting for dismiss/snooze
};

struct Reminder {
    uint8_t hour;                       // 0-23
    uint8_t minute;                     // 0-59
    char message[REMINDER_MAX_MESSAGE + 1]; // null-terminated
    bool recurring;                     // true = fires daily
    bool enabled;                       // active flag
};

class ReminderManager {
public:
    ReminderManager();

    void begin();

    /**
     * @brief Update state machine (call every frame)
     * @param dt Delta time in seconds
     * @param currentHour Current hour (0-23)
     * @param currentMinute Current minute (0-59)
     * @return true if state changed
     */
    bool update(float dt, int currentHour, int currentMinute);

    // Reminder management
    bool add(uint8_t hour, uint8_t minute, const char* message, bool recurring = false);
    void remove(int index);
    bool removeByMessage(const char* substring);

    // User actions during Showing state
    void dismiss();
    void snooze();

    // State queries
    ReminderState getState() const { return state; }
    bool isShowing() const { return state == ReminderState::Showing; }
    const Reminder* getActiveReminder() const;
    int getReminderCount() const { return reminders.size(); }
    int getMaxReminders() const { return REMINDER_MAX_COUNT; }
    const std::vector<Reminder>& getReminders() const { return reminders; }

    /**
     * @brief Check if there's a pending state change from external action
     * @return true if state changed externally (clears the flag)
     */
    bool consumeExternalStateChange();

    /**
     * @brief Set whether another full-screen feature is active
     * Reminders won't trigger during pomodoro, countdown, breathing, or menu
     */
    void setBlocked(bool blocked) { isBlocked = blocked; }

    /**
     * @brief Render the reminder prompt screen to buffer
     */
    void renderPrompt(uint16_t* buffer, int16_t bufW, int16_t bufH, uint16_t eyeColor);

private:
    ReminderState state;
    std::vector<Reminder> reminders;
    int activeIndex;                // Index of currently showing reminder
    uint32_t showStartTime;         // When the prompt appeared (millis)
    uint32_t snoozeUntil;           // Millis timestamp for snoozed reminder
    int snoozedIndex;               // Which reminder was snoozed
    int8_t lastTriggeredMinute;     // Avoid re-triggering in same minute
    int8_t lastTriggeredHour;
    bool externalStateChange;
    bool isBlocked;

    Preferences prefs;

    void loadFromNVS();
    void saveToNVS();

    // Rendering helpers (same pattern as BreathingExercise)
    void drawFilledRect(uint16_t* buffer, int16_t bufW, int16_t bufH,
                        int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawChar(uint16_t* buffer, int16_t bufW, int16_t bufH,
                  int16_t x, int16_t y, char c, uint16_t color, int scale = 3);
    void drawText(uint16_t* buffer, int16_t bufW, int16_t bufH,
                  int16_t x, int16_t y, const char* text, uint16_t color, int scale = 3);
    void drawCenteredText(uint16_t* buffer, int16_t bufW, int16_t bufH,
                          int16_t centerX, int16_t y, const char* text, uint16_t color, int scale = 3);
    void drawWrappedText(uint16_t* buffer, int16_t bufW, int16_t bufH,
                         int16_t centerX, int16_t startY, const char* text,
                         uint16_t color, int scale, int maxCharsPerLine);
};

#endif // REMINDER_MANAGER_H
