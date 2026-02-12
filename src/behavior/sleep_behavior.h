/**
 * Sleep Behavior - Energy-saving mode with breathing animation
 * Tracks idle time and manages sleep/wake transitions
 */

#ifndef SLEEP_BEHAVIOR_H
#define SLEEP_BEHAVIOR_H

#include <Arduino.h>

// Sleep state machine
enum class SleepState {
    Awake,          // Normal operation
    Drowsy,         // Getting sleepy (lids drooping)
    FallingAsleep,  // Final blink, closing eyes
    Sleeping,       // Breathing bar animation
    WakingUp        // Snap awake
};

/**
 * Sleep Behavior - manages sleep/wake cycle
 */
class SleepBehavior {
public:
    SleepBehavior();

    /**
     * Initialize sleep behavior
     */
    void begin();

    /**
     * Update sleep state (call every frame)
     * @param dt Delta time in seconds
     * @param hasInteraction True if user is interacting (touch, loud sound, etc.)
     * @param hasMotion True if device moved (IMU detected)
     */
    void update(float dt, bool hasInteraction, bool hasMotion);

    /**
     * Get current sleep state
     */
    SleepState getState() const { return state; }

    /**
     * Check if currently sleeping (showing breathing bars)
     */
    bool isSleeping() const { return state == SleepState::Sleeping; }

    /**
     * Check if drowsy (should show sleepy expression)
     */
    bool isDrowsy() const { return state == SleepState::Drowsy; }

    /**
     * Check if in snap-wide moment (briefly alert during drowsy)
     */
    bool isSnapWide() const { return snapWideActive; }

    /**
     * Check if waking up (should show surprised expression)
     */
    bool isWakingUp() const { return state == SleepState::WakingUp; }

    /**
     * Check if falling asleep (final transition to sleep)
     */
    bool isFallingAsleep() const { return state == SleepState::FallingAsleep; }

    /**
     * Get drowsiness level (0.0 = awake, 1.0 = fully drowsy)
     * Use to interpolate toward sleepy expression
     */
    float getDrowsiness() const { return drowsiness; }

    /**
     * Get breathing phase for sleep animation (0.0 - 1.0)
     */
    float getBreathingPhase() const { return breathingPhase; }

    /**
     * Get breathing bar brightness (0.0 - 1.0)
     */
    float getBreathingBrightness() const;

    /**
     * Force wake up (e.g., from external trigger)
     */
    void wakeUp();

    /**
     * Force sleep (for testing)
     */
    void forceSleep();

    /**
     * Set idle timeout before drowsiness starts
     */
    void setIdleTimeout(uint32_t ms) { idleTimeout = ms; }

    /**
     * Set drowsy duration before falling asleep
     */
    void setDrowsyDuration(uint32_t ms) { drowsyDuration = ms; }

private:
    void enterState(SleepState newState);
    void updateAwake(float dt, bool hasInteraction);
    void updateDrowsy(float dt, bool hasInteraction, bool hasMotion);
    void updateFallingAsleep(float dt);
    void updateSleeping(float dt, bool hasInteraction, bool hasMotion);
    void updateWakingUp(float dt);

    SleepState state;
    uint32_t stateStartTime;
    uint32_t lastInteractionTime;

    // Drowsiness (0.0 - 1.0)
    float drowsiness;
    float targetDrowsiness;

    // Breathing animation
    float breathingPhase;

    // Snap-wide (brief alert during drowsy)
    bool snapWideActive;
    uint32_t snapWideStart;
    uint32_t nextSnapWideTime;

    // Timing configuration
    uint32_t idleTimeout;       // Time before getting drowsy (default 30s)
    uint32_t drowsyDuration;    // Time in drowsy before falling asleep (default 10s)
    uint32_t fallAsleepDuration; // Time to close eyes (default 2s)
    uint32_t wakeUpDuration;    // Time for wake animation (default 0.5s)

    // Breathing animation timing
    static constexpr float BREATHING_CYCLE = 3.5f;  // 3.5 second breath cycle
};

#endif // SLEEP_BEHAVIOR_H
