/**
 * Idle Behavior - Autonomous lifelike movements when not actively engaged
 * Includes gaze scanning, varied blinks, and micro-movements
 */

#ifndef IDLE_BEHAVIOR_H
#define IDLE_BEHAVIOR_H

#include <Arduino.h>

class IdleBehavior {
public:
    IdleBehavior();

    /**
     * Initialize with random seed
     */
    void begin();

    /**
     * Update idle behaviors (call every frame)
     * @param dt Delta time in seconds
     * @param isEngaged True if user is interacting (suppresses some idle behaviors)
     */
    void update(float dt, bool isEngaged);

    /**
     * Get current gaze offset from idle scanning
     * These should be ADDED to any active gaze target
     */
    float getIdleGazeX() const { return idleGazeX; }
    float getIdleGazeY() const { return idleGazeY; }

    /**
     * Get micro-movement offset
     * Very small jitter to add liveliness
     */
    float getMicroX() const { return microX; }
    float getMicroY() const { return microY; }

    /**
     * Check if a blink should be triggered
     * Returns true once per blink event, then resets
     */
    bool shouldBlink();

    /**
     * Check if this is a double-blink
     * If shouldBlink() returns true and this is true, blink twice quickly
     */
    bool isDoubleBlink() const { return doubleBlink; }

    /**
     * Get blink speed multiplier (for varied blink speeds)
     * 1.0 = normal, < 1.0 = slower, > 1.0 = faster
     */
    float getBlinkSpeed() const { return blinkSpeed; }

    /**
     * Enable/disable idle behaviors
     */
    void setEnabled(bool enabled) { this->enabled = enabled; }
    bool isEnabled() const { return enabled; }

    /**
     * Set mood-based multipliers (from time_mood.h)
     * @param blinkMult Blink rate multiplier (>1 = more frequent)
     * @param gazeMult Gaze speed multiplier (<1 = slower)
     */
    void setMoodModifiers(float blinkMult, float gazeMult) {
        blinkRateMultiplier = blinkMult;
        gazeSpeedMultiplier = gazeMult;
    }

    /**
     * Yawn behavior (triggered after 30-40 min idle)
     */
    bool shouldYawn();               // Check if yawn should be triggered
    float getYawnProgress() const { return yawnProgress; }  // 0.0 to 1.0
    bool isYawning() const { return yawnActive; }
    void notifyActivity();           // Reset idle timer on user activity

private:
    // Gaze scanning
    float idleGazeX, idleGazeY;
    float targetGazeX, targetGazeY;
    float gazeVelocityX, gazeVelocityY;
    uint32_t lastGazeChange;
    uint32_t nextGazeInterval;

    // Micro-movements
    float microX, microY;
    float microPhase;

    // Blinking
    uint32_t lastBlinkTime;
    uint32_t nextBlinkInterval;
    bool blinkPending;
    bool doubleBlink;
    float blinkSpeed;

    // Yawn behavior
    uint32_t lastActivityTime;       // Last time user interacted
    uint32_t yawnTriggerTime;        // When yawn should trigger (random 30-40 min)
    bool yawnPending;                // Yawn should start
    bool yawnActive;                 // Currently yawning
    float yawnProgress;              // 0.0 to 1.0 during yawn animation
    uint32_t yawnStartTime;          // When yawn animation started

    // State
    bool enabled;

    // Mood modifiers
    float blinkRateMultiplier;
    float gazeSpeedMultiplier;

    // Pick new random gaze target
    void pickNewGazeTarget();

    // Update gaze interpolation
    void updateGaze(float dt);

    // Update micro-movement
    void updateMicro(float dt);

    // Update blink timing
    void updateBlink(float dt);
};

#endif // IDLE_BEHAVIOR_H
