/**
 * @file pomodoro.cpp
 * @brief Pomodoro timer implementation
 */

#include "pomodoro.h"

// Default settings
#define DEFAULT_WORK_MINUTES 25
#define DEFAULT_SHORT_BREAK 5
#define DEFAULT_LONG_BREAK 15
#define DEFAULT_SESSIONS 4

// Celebration duration
#define CELEBRATION_DURATION_MS 2000

PomodoroTimer::PomodoroTimer()
    : state(PomodoroState::Idle)
    , sessionStartTime(0)
    , sessionDuration(0)
    , sessionNumber(0)
    , celebrationDone(false)
    , workMinutes(DEFAULT_WORK_MINUTES)
    , shortBreakMinutes(DEFAULT_SHORT_BREAK)
    , longBreakMinutes(DEFAULT_LONG_BREAK)
    , sessionsBeforeLongBreak(DEFAULT_SESSIONS)
    , tickingEnabled(true) {
}

void PomodoroTimer::begin() {
    loadSettings();
    Serial.println("Pomodoro timer initialized");
}

void PomodoroTimer::loadSettings() {
    prefs.begin("pomodoro", true);  // Read-only
    workMinutes = prefs.getInt("work", DEFAULT_WORK_MINUTES);
    shortBreakMinutes = prefs.getInt("short", DEFAULT_SHORT_BREAK);
    longBreakMinutes = prefs.getInt("long", DEFAULT_LONG_BREAK);
    sessionsBeforeLongBreak = prefs.getInt("sessions", DEFAULT_SESSIONS);
    tickingEnabled = prefs.getBool("tick", true);
    prefs.end();
}

void PomodoroTimer::saveSettings() {
    prefs.begin("pomodoro", false);  // Read-write
    prefs.putInt("work", workMinutes);
    prefs.putInt("short", shortBreakMinutes);
    prefs.putInt("long", longBreakMinutes);
    prefs.putInt("sessions", sessionsBeforeLongBreak);
    prefs.putBool("tick", tickingEnabled);
    prefs.end();
}

void PomodoroTimer::setWorkMinutes(int minutes) {
    workMinutes = constrain(minutes, 1, 60);
    saveSettings();
}

void PomodoroTimer::setShortBreakMinutes(int minutes) {
    shortBreakMinutes = constrain(minutes, 1, 30);
    saveSettings();
}

void PomodoroTimer::setLongBreakMinutes(int minutes) {
    longBreakMinutes = constrain(minutes, 1, 60);
    saveSettings();
}

void PomodoroTimer::setSessionsBeforeLongBreak(int sessions) {
    sessionsBeforeLongBreak = constrain(sessions, 1, 8);
    saveSettings();
}

void PomodoroTimer::setTickingEnabled(bool enabled) {
    tickingEnabled = enabled;
    saveSettings();
}

uint32_t PomodoroTimer::getSessionDurationMs() const {
    switch (state) {
        case PomodoroState::Working:
            return (uint32_t)workMinutes * 60 * 1000;
        case PomodoroState::ShortBreak:
            return (uint32_t)shortBreakMinutes * 60 * 1000;
        case PomodoroState::LongBreak:
            return (uint32_t)longBreakMinutes * 60 * 1000;
        case PomodoroState::Celebration:
            return CELEBRATION_DURATION_MS;
        default:
            return 0;
    }
}

void PomodoroTimer::startSession(PomodoroState newState) {
    state = newState;
    sessionStartTime = millis();
    sessionDuration = getSessionDurationMs();
    celebrationDone = false;

    const char* stateNames[] = {"Idle", "Working", "ShortBreak", "LongBreak", "Celebration", "WaitingForTap"};
    Serial.printf("Pomodoro: Starting %s (duration: %lu ms)\n",
                  stateNames[(int)newState], sessionDuration);
}

void PomodoroTimer::start() {
    sessionNumber = 1;
    startSession(PomodoroState::Working);
    Serial.println("Pomodoro: Started work session 1");
}

void PomodoroTimer::stop() {
    state = PomodoroState::Idle;
    sessionNumber = 0;
    sessionStartTime = 0;
    sessionDuration = 0;
    celebrationDone = false;
    Serial.println("Pomodoro: Stopped");
}

void PomodoroTimer::onTap() {
    if (state == PomodoroState::WaitingForTap) {
        // Determine next phase
        if (sessionNumber > 0 && sessionNumber < sessionsBeforeLongBreak) {
            // Start next work session after short break
            sessionNumber++;
            startSession(PomodoroState::Working);
        } else if (sessionNumber >= sessionsBeforeLongBreak) {
            // After long break, start new cycle
            sessionNumber = 1;
            startSession(PomodoroState::Working);
        }
    } else if (state == PomodoroState::Idle) {
        // Start fresh
        start();
    }
}

float PomodoroTimer::getProgress() const {
    if (state == PomodoroState::Idle || state == PomodoroState::WaitingForTap) {
        return 1.0f;  // Full progress bar when idle/waiting
    }

    if (sessionDuration == 0) return 1.0f;

    uint32_t elapsed = millis() - sessionStartTime;
    float progress = 1.0f - ((float)elapsed / (float)sessionDuration);
    return constrain(progress, 0.0f, 1.0f);
}

uint32_t PomodoroTimer::getRemainingSeconds() const {
    if (state == PomodoroState::Idle || state == PomodoroState::WaitingForTap) {
        return 0;
    }

    uint32_t elapsed = millis() - sessionStartTime;
    if (elapsed >= sessionDuration) return 0;

    return (sessionDuration - elapsed) / 1000;
}

bool PomodoroTimer::isLastMinute() const {
    if (state != PomodoroState::Working && state != PomodoroState::ShortBreak &&
        state != PomodoroState::LongBreak) {
        return false;
    }
    return getRemainingSeconds() <= 60 && getRemainingSeconds() > 0;
}

bool PomodoroTimer::update(float dt) {
    if (state == PomodoroState::Idle || state == PomodoroState::WaitingForTap) {
        return false;
    }

    uint32_t now = millis();
    uint32_t elapsed = now - sessionStartTime;

    // Check if session is complete
    if (elapsed >= sessionDuration) {
        if (state == PomodoroState::Celebration) {
            // Celebration done - start the appropriate break
            if (!celebrationDone) {
                // This was after a work session - start break
                if (sessionNumber >= sessionsBeforeLongBreak) {
                    startSession(PomodoroState::LongBreak);
                    Serial.println("Pomodoro: Starting long break");
                } else {
                    startSession(PomodoroState::ShortBreak);
                    Serial.println("Pomodoro: Starting short break");
                }
                celebrationDone = true;
            } else {
                // This was after a break - wait for tap to start next work
                state = PomodoroState::WaitingForTap;
                Serial.println("Pomodoro: Waiting for tap to continue");
            }
            return true;
        }

        // Work or break session complete - start celebration
        if (state == PomodoroState::Working) {
            Serial.printf("Pomodoro: Work session %d complete\n", sessionNumber);
            celebrationDone = false;  // Next will go to break
            startSession(PomodoroState::Celebration);
        } else if (state == PomodoroState::ShortBreak || state == PomodoroState::LongBreak) {
            Serial.println("Pomodoro: Break complete");
            celebrationDone = true;  // Next will wait for tap
            startSession(PomodoroState::Celebration);
        }

        return true;  // State changed
    }

    return false;
}
