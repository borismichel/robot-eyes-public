/**
 * @file countdown_timer.cpp
 * @brief Simple countdown timer implementation
 */

#include "countdown_timer.h"

static const uint32_t CELEBRATION_DURATION_MS = 2000;

CountdownTimer::CountdownTimer()
    : state(CountdownState::Idle)
    , startTime(0)
    , duration(0)
    , tickingEnabled(true)
    , celebrationStart(0)
{
}

void CountdownTimer::begin() {
    prefs.begin("timer", true);  // read-only
    tickingEnabled = prefs.getBool("ticking", true);
    prefs.end();
}

bool CountdownTimer::update(float dt) {
    CountdownState prevState = state;

    if (state == CountdownState::Running) {
        uint32_t elapsed = millis() - startTime;
        if (elapsed >= duration) {
            state = CountdownState::Celebration;
            celebrationStart = millis();
            Serial.printf("[Timer] %s finished!\n", timerName.c_str());
        }
    } else if (state == CountdownState::Celebration) {
        if (millis() - celebrationStart >= CELEBRATION_DURATION_MS) {
            state = CountdownState::Idle;
            Serial.println("[Timer] Celebration done, returning to idle");
        }
    }

    return state != prevState;
}

void CountdownTimer::start(uint32_t seconds, const char* name) {
    if (seconds == 0) return;

    duration = seconds * 1000;
    startTime = millis();
    timerName = name ? name : "TIMER";
    state = CountdownState::Running;

    Serial.printf("[Timer] Started: %lu seconds (%s)\n", seconds, timerName.c_str());
}

void CountdownTimer::stop() {
    if (state == CountdownState::Idle) return;
    state = CountdownState::Idle;
    Serial.println("[Timer] Stopped");
}

uint32_t CountdownTimer::getRemainingSeconds() const {
    if (state != CountdownState::Running) return 0;

    uint32_t elapsed = millis() - startTime;
    if (elapsed >= duration) return 0;
    return (duration - elapsed + 999) / 1000;  // Round up
}

float CountdownTimer::getProgress() const {
    if (state != CountdownState::Running) return 1.0f;

    uint32_t elapsed = millis() - startTime;
    if (elapsed >= duration) return 0.0f;
    return 1.0f - ((float)elapsed / (float)duration);
}

bool CountdownTimer::isLastMinute() const {
    if (state != CountdownState::Running) return false;
    return getRemainingSeconds() <= 60;
}

void CountdownTimer::setTickingEnabled(bool enabled) {
    tickingEnabled = enabled;
    prefs.begin("timer", false);
    prefs.putBool("ticking", enabled);
    prefs.end();
}
