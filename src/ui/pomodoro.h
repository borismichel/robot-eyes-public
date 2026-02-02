/**
 * @file pomodoro.h
 * @brief Pomodoro timer state machine
 *
 * Implements focus timer with work/break cycles:
 * - Work session (default 25 min)
 * - Short break (default 5 min) after each work session
 * - Long break (default 15 min) after 4 work sessions
 */

#ifndef POMODORO_H
#define POMODORO_H

#include <Arduino.h>
#include <Preferences.h>

/**
 * Pomodoro session states
 */
enum class PomodoroState {
    Idle,           // Not running, waiting to start
    Working,        // Work session in progress
    ShortBreak,     // Short break in progress
    LongBreak,      // Long break in progress
    Celebration,    // Session complete, showing celebration
    WaitingForTap   // Waiting for user tap to start next phase
};

/**
 * Pomodoro timer class
 */
class PomodoroTimer {
public:
    PomodoroTimer();

    /**
     * Initialize with saved settings
     */
    void begin();

    /**
     * Update timer state (call every frame)
     * @param dt Delta time in seconds
     * @return true if timer state changed (for UI updates)
     */
    bool update(float dt);

    /**
     * Start a work session
     */
    void start();

    /**
     * Stop/reset the timer
     */
    void stop();

    /**
     * Handle tap input (advance to next phase when waiting)
     */
    void onTap();

    /**
     * Check if pomodoro is active (not idle)
     */
    bool isActive() const { return state != PomodoroState::Idle; }

    /**
     * Get current state
     */
    PomodoroState getState() const { return state; }

    /**
     * Get progress (0.0 to 1.0, depletes over time)
     */
    float getProgress() const;

    /**
     * Get remaining time in seconds
     */
    uint32_t getRemainingSeconds() const;

    /**
     * Get current session number (1-4)
     */
    int getSessionNumber() const { return sessionNumber; }

    /**
     * Check if in last 60 seconds (for ticking)
     */
    bool isLastMinute() const;

    /**
     * Check if ticking is enabled
     */
    bool isTickingEnabled() const { return tickingEnabled; }

    // Settings getters
    int getWorkMinutes() const { return workMinutes; }
    int getShortBreakMinutes() const { return shortBreakMinutes; }
    int getLongBreakMinutes() const { return longBreakMinutes; }
    int getSessionsBeforeLongBreak() const { return sessionsBeforeLongBreak; }

    // Settings setters (save to preferences)
    void setWorkMinutes(int minutes);
    void setShortBreakMinutes(int minutes);
    void setLongBreakMinutes(int minutes);
    void setSessionsBeforeLongBreak(int sessions);
    void setTickingEnabled(bool enabled);

private:
    PomodoroState state;
    uint32_t sessionStartTime;      // When current session started (ms)
    uint32_t sessionDuration;       // Duration of current session (ms)
    int sessionNumber;              // Current work session (1-4)
    bool celebrationDone;           // Has celebration been shown

    // Settings (persisted)
    int workMinutes;                // Default 25
    int shortBreakMinutes;          // Default 5
    int longBreakMinutes;           // Default 15
    int sessionsBeforeLongBreak;    // Default 4
    bool tickingEnabled;            // Tick sound in last 60s

    Preferences prefs;

    void loadSettings();
    void saveSettings();
    void startSession(PomodoroState newState);
    uint32_t getSessionDurationMs() const;
};

#endif // POMODORO_H
