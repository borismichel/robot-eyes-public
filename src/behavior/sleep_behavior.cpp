/**
 * Sleep Behavior Implementation
 * Manages sleep/wake cycle with breathing animation
 */

#include "sleep_behavior.h"
#include <cmath>

// Snap-wide timing
#define SNAP_WIDE_MIN_INTERVAL 3000   // Min time between snap-wides (ms)
#define SNAP_WIDE_MAX_INTERVAL 8000   // Max time between snap-wides (ms)
#define SNAP_WIDE_DURATION     500    // Duration of snap-wide moment (ms)

SleepBehavior::SleepBehavior()
    : state(SleepState::Awake)
    , stateStartTime(0)
    , lastInteractionTime(0)
    , drowsiness(0.0f)
    , targetDrowsiness(0.0f)
    , breathingPhase(0.0f)
    , snapWideActive(false)
    , snapWideStart(0)
    , nextSnapWideTime(0)
    , idleTimeout(1800000)       // 30 minutes
    , drowsyDuration(120000)     // 2 minutes
    , fallAsleepDuration(2000)   // 2 seconds
    , wakeUpDuration(500) {      // 0.5 seconds
}

void SleepBehavior::begin() {
    state = SleepState::Awake;
    stateStartTime = millis();
    lastInteractionTime = millis();
    drowsiness = 0.0f;
    targetDrowsiness = 0.0f;
    breathingPhase = 0.0f;
    snapWideActive = false;
    snapWideStart = 0;
    nextSnapWideTime = SNAP_WIDE_MIN_INTERVAL + random(SNAP_WIDE_MAX_INTERVAL - SNAP_WIDE_MIN_INTERVAL);
}

void SleepBehavior::enterState(SleepState newState) {
    state = newState;
    stateStartTime = millis();

    // Reset snap-wide when entering drowsy state
    if (newState == SleepState::Drowsy) {
        snapWideActive = false;
        nextSnapWideTime = millis() + SNAP_WIDE_MIN_INTERVAL + random(SNAP_WIDE_MAX_INTERVAL - SNAP_WIDE_MIN_INTERVAL);
    }

    const char* stateNames[] = {"Awake", "Drowsy", "FallingAsleep", "Sleeping", "WakingUp"};
    Serial.printf("Sleep state: %s\n", stateNames[(int)newState]);
}

void SleepBehavior::update(float dt, bool hasInteraction, bool hasMotion) {
    // Reset interaction timer on any activity
    if (hasInteraction || hasMotion) {
        lastInteractionTime = millis();
    }

    switch (state) {
        case SleepState::Awake:
            updateAwake(dt, hasInteraction);
            break;
        case SleepState::Drowsy:
            updateDrowsy(dt, hasInteraction, hasMotion);
            break;
        case SleepState::FallingAsleep:
            updateFallingAsleep(dt);
            break;
        case SleepState::Sleeping:
            updateSleeping(dt, hasInteraction, hasMotion);
            break;
        case SleepState::WakingUp:
            updateWakingUp(dt);
            break;
    }

    // Smooth drowsiness transitions
    float smoothSpeed = 2.0f;  // Reach target in ~0.5s
    drowsiness += (targetDrowsiness - drowsiness) * smoothSpeed * dt;
    drowsiness = constrain(drowsiness, 0.0f, 1.0f);
}

void SleepBehavior::updateAwake(float dt, bool hasInteraction) {
    targetDrowsiness = 0.0f;

    // Check if idle long enough to get drowsy
    uint32_t idleTime = millis() - lastInteractionTime;
    if (idleTime >= idleTimeout) {
        enterState(SleepState::Drowsy);
    }
}

void SleepBehavior::updateDrowsy(float dt, bool hasInteraction, bool hasMotion) {
    uint32_t now = millis();
    uint32_t timeInState = now - stateStartTime;

    // Calculate drowsiness based on time in state
    float progress = (float)timeInState / (float)drowsyDuration;
    targetDrowsiness = constrain(progress, 0.0f, 1.0f);

    // Handle snap-wide behavior (brief moment of alertness)
    if (snapWideActive) {
        // During snap-wide, reduce drowsiness temporarily
        targetDrowsiness = 0.0f;

        // Check if snap-wide should end
        if (now - snapWideStart >= SNAP_WIDE_DURATION) {
            snapWideActive = false;
            nextSnapWideTime = now + SNAP_WIDE_MIN_INTERVAL + random(SNAP_WIDE_MAX_INTERVAL - SNAP_WIDE_MIN_INTERVAL);
            Serial.println("Snap-wide ended");
        }
    } else if (now >= nextSnapWideTime && timeInState > 2000) {
        // Trigger snap-wide (only after being drowsy for at least 2s)
        snapWideActive = true;
        snapWideStart = now;
        Serial.println("Snap-wide! (brief alertness)");
    }

    // Wake up on interaction
    if (hasInteraction || hasMotion) {
        snapWideActive = false;
        enterState(SleepState::Awake);
        return;
    }

    // Transition to falling asleep
    if (timeInState >= drowsyDuration) {
        snapWideActive = false;
        enterState(SleepState::FallingAsleep);
    }
}

void SleepBehavior::updateFallingAsleep(float dt) {
    uint32_t timeInState = millis() - stateStartTime;

    // Full drowsiness while falling asleep
    targetDrowsiness = 1.0f;

    // Transition to sleeping after eyes close
    if (timeInState >= fallAsleepDuration) {
        enterState(SleepState::Sleeping);
    }
}

void SleepBehavior::updateSleeping(float dt, bool hasInteraction, bool hasMotion) {
    targetDrowsiness = 1.0f;

    // Update breathing animation
    breathingPhase += dt / BREATHING_CYCLE;
    if (breathingPhase >= 1.0f) {
        breathingPhase -= 1.0f;
    }

    // Wake up on interaction or motion
    if (hasInteraction || hasMotion) {
        enterState(SleepState::WakingUp);
    }
}

void SleepBehavior::updateWakingUp(float dt) {
    uint32_t timeInState = millis() - stateStartTime;

    // Quickly reduce drowsiness
    float progress = (float)timeInState / (float)wakeUpDuration;
    targetDrowsiness = 1.0f - constrain(progress, 0.0f, 1.0f);

    // Back to awake
    if (timeInState >= wakeUpDuration) {
        enterState(SleepState::Awake);
    }
}

float SleepBehavior::getBreathingBrightness() const {
    if (state != SleepState::Sleeping) {
        return 0.0f;
    }

    // Smooth sine wave for breathing effect
    // Phase 0.0-0.5: inhale (dim to bright)
    // Phase 0.5-1.0: exhale (bright to dim)
    float brightness = sinf(breathingPhase * 2.0f * M_PI) * 0.5f + 0.5f;

    // Apply easing for more organic feel
    // Range from 0.2 (dim) to 1.0 (bright)
    return 0.2f + brightness * 0.8f;
}

void SleepBehavior::wakeUp() {
    if (state == SleepState::Sleeping || state == SleepState::FallingAsleep) {
        enterState(SleepState::WakingUp);
    } else if (state == SleepState::Drowsy) {
        enterState(SleepState::Awake);
    }
    lastInteractionTime = millis();
}

void SleepBehavior::forceSleep() {
    enterState(SleepState::Sleeping);
    drowsiness = 1.0f;
    targetDrowsiness = 1.0f;
}
