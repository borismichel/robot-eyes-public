/**
 * @file countdown_timer.h
 * @brief Simple countdown timer for DeskBuddy
 *
 * One-shot countdown timer that shows MM:SS on screen,
 * ticks in the last 60 seconds, and celebrates when done.
 * No work/break cycles — just countdown and finish.
 */

#ifndef COUNTDOWN_TIMER_H
#define COUNTDOWN_TIMER_H

#include <Arduino.h>
#include <Preferences.h>

enum class CountdownState {
    Idle,           // Not running
    Running,        // Counting down
    Celebration     // Timer finished, showing happy (2 seconds)
};

class CountdownTimer {
public:
    CountdownTimer();

    /** Initialize and load settings from Preferences */
    void begin();

    /**
     * Update timer state (call every frame)
     * @param dt Delta time in seconds
     * @return true if state changed
     */
    bool update(float dt);

    /**
     * Start a countdown
     * @param seconds Duration in seconds
     * @param name Display label (shown above countdown)
     */
    void start(uint32_t seconds, const char* name = "TIMER");

    /** Stop/cancel the timer */
    void stop();

    // State queries
    CountdownState getState() const { return state; }
    bool isActive() const { return state != CountdownState::Idle; }
    uint32_t getRemainingSeconds() const;
    float getProgress() const;
    bool isLastMinute() const;
    const char* getTimerName() const { return timerName.c_str(); }

    // Settings
    bool isTickingEnabled() const { return tickingEnabled; }
    void setTickingEnabled(bool enabled);

private:
    CountdownState state;
    uint32_t startTime;         // millis() when started
    uint32_t duration;          // Duration in milliseconds
    String timerName;           // Display label
    bool tickingEnabled;        // Tick sound in last 60s

    uint32_t celebrationStart;  // millis() when celebration began

    Preferences prefs;
};

#endif // COUNTDOWN_TIMER_H
