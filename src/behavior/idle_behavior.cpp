/**
 * Idle Behavior Implementation
 * Creates lifelike autonomous movements
 */

#include "idle_behavior.h"
#include <cmath>

// Gaze scanning parameters (saccadic eye movements)
#define GAZE_MIN_INTERVAL 1500   // Minimum time between gaze shifts (ms)
#define GAZE_MAX_INTERVAL 3000   // Maximum time between gaze shifts (ms)
#define GAZE_MAX_OFFSET   0.95f  // Maximum gaze offset (-0.95 to 0.95)
#define GAZE_SMOOTH_TIME  0.08f  // Fast saccadic movement (80ms)

// Micro-movement parameters
#define MICRO_AMPLITUDE   0.02f  // Very small movements
#define MICRO_FREQUENCY   0.8f   // Oscillation frequency (Hz)

// Blink parameters (6-10 per minute = 6-10s intervals)
#define BLINK_MIN_INTERVAL 6000  // Minimum time between blinks (ms)
#define BLINK_MAX_INTERVAL 10000 // Maximum time between blinks (ms)
#define DOUBLE_BLINK_CHANCE 0.15f // 15% chance of double-blink

// Yawn parameters (30-40 min idle triggers yawn)
#define YAWN_MIN_IDLE      1800000  // 30 minutes in ms
#define YAWN_MAX_IDLE      2400000  // 40 minutes in ms
#define YAWN_DURATION      2000     // 2 second yawn animation

IdleBehavior::IdleBehavior()
    : idleGazeX(0)
    , idleGazeY(0)
    , targetGazeX(0)
    , targetGazeY(0)
    , gazeVelocityX(0)
    , gazeVelocityY(0)
    , lastGazeChange(0)
    , nextGazeInterval(GAZE_MIN_INTERVAL)
    , microX(0)
    , microY(0)
    , microPhase(0)
    , lastBlinkTime(0)
    , nextBlinkInterval(BLINK_MIN_INTERVAL)
    , blinkPending(false)
    , doubleBlink(false)
    , blinkSpeed(1.0f)
    , lastActivityTime(0)
    , yawnTriggerTime(YAWN_MIN_IDLE)
    , yawnPending(false)
    , yawnActive(false)
    , yawnProgress(0.0f)
    , yawnStartTime(0)
    , enabled(true)
    , blinkRateMultiplier(1.0f)
    , gazeSpeedMultiplier(1.0f) {
}

void IdleBehavior::begin() {
    // Randomize initial state
    lastGazeChange = millis();
    lastBlinkTime = millis();
    lastActivityTime = millis();

    nextGazeInterval = GAZE_MIN_INTERVAL + random(GAZE_MAX_INTERVAL - GAZE_MIN_INTERVAL);
    nextBlinkInterval = BLINK_MIN_INTERVAL + random(BLINK_MAX_INTERVAL - BLINK_MIN_INTERVAL);

    // Random yawn trigger time between 30-40 minutes
    yawnTriggerTime = YAWN_MIN_IDLE + random(YAWN_MAX_IDLE - YAWN_MIN_IDLE);

    // Random initial micro phase
    microPhase = random(1000) / 1000.0f * 2.0f * PI;
}

void IdleBehavior::update(float dt, bool isEngaged) {
    if (!enabled) {
        idleGazeX = 0;
        idleGazeY = 0;
        microX = 0;
        microY = 0;
        return;
    }

    uint32_t now = millis();

    // Update components
    updateGaze(dt);
    updateMicro(dt);
    updateBlink(dt);

    // Update yawn animation if active
    if (yawnActive) {
        uint32_t elapsed = now - yawnStartTime;
        yawnProgress = (float)elapsed / (float)YAWN_DURATION;
        if (yawnProgress >= 1.0f) {
            yawnProgress = 1.0f;
            yawnActive = false;
        }
    }

    // Check if it's time to yawn (idle for 30-40 minutes)
    if (!yawnPending && !yawnActive && (now - lastActivityTime > yawnTriggerTime)) {
        yawnPending = true;
        Serial.println("Yawn triggered after idle timeout");
    }

    // When engaged, reduce idle gaze influence
    if (isEngaged) {
        // Smoothly reduce idle gaze when user is interacting
        idleGazeX *= 0.95f;
        idleGazeY *= 0.95f;
    }
}

void IdleBehavior::pickNewGazeTarget() {
    // Random gaze target within bounds
    // Bias towards center (more likely to look near center)
    float randX = (random(2001) - 1000) / 1000.0f;  // -1 to 1
    float randY = (random(2001) - 1000) / 1000.0f;

    // Apply bias towards center (square the random value, keep sign)
    randX = (randX >= 0 ? 1 : -1) * randX * randX;
    randY = (randY >= 0 ? 1 : -1) * randY * randY;

    targetGazeX = randX * GAZE_MAX_OFFSET;
    targetGazeY = randY * GAZE_MAX_OFFSET;

    // Occasionally look back to center
    if (random(100) < 30) {  // 30% chance
        targetGazeX = 0;
        targetGazeY = 0;
    }
}

void IdleBehavior::updateGaze(float dt) {
    uint32_t now = millis();

    // Check if it's time for a new gaze target (apply mood - lower = slower = longer interval)
    uint32_t adjustedInterval = (uint32_t)(nextGazeInterval / gazeSpeedMultiplier);
    if (now - lastGazeChange > adjustedInterval) {
        pickNewGazeTarget();
        lastGazeChange = now;
        nextGazeInterval = GAZE_MIN_INTERVAL + random(GAZE_MAX_INTERVAL - GAZE_MIN_INTERVAL);
    }

    // Smooth damp towards target (apply mood to smooth time)
    float adjustedSmoothTime = GAZE_SMOOTH_TIME / gazeSpeedMultiplier;
    float omega = 2.0f / adjustedSmoothTime;
    float x = omega * dt;
    float exp_term = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

    // X axis
    float deltaX = idleGazeX - targetGazeX;
    float tempX = (gazeVelocityX + omega * deltaX) * dt;
    gazeVelocityX = (gazeVelocityX - omega * tempX) * exp_term;
    idleGazeX = targetGazeX + (deltaX + tempX) * exp_term;

    // Y axis
    float deltaY = idleGazeY - targetGazeY;
    float tempY = (gazeVelocityY + omega * deltaY) * dt;
    gazeVelocityY = (gazeVelocityY - omega * tempY) * exp_term;
    idleGazeY = targetGazeY + (deltaY + tempY) * exp_term;
}

void IdleBehavior::updateMicro(float dt) {
    // Advance phase
    microPhase += dt * MICRO_FREQUENCY * 2.0f * PI;
    if (microPhase > 2.0f * PI) {
        microPhase -= 2.0f * PI;
    }

    // Lissajous-like pattern for organic movement
    microX = sinf(microPhase) * MICRO_AMPLITUDE;
    microY = sinf(microPhase * 1.3f + 0.5f) * MICRO_AMPLITUDE;
}

void IdleBehavior::updateBlink(float dt) {
    uint32_t now = millis();

    // Check if it's time for a blink
    if (!blinkPending && (now - lastBlinkTime > nextBlinkInterval)) {
        blinkPending = true;

        // Determine blink characteristics
        doubleBlink = (random(100) < (int)(DOUBLE_BLINK_CHANCE * 100));
        blinkSpeed = 0.8f + (random(40) / 100.0f);  // 0.8 to 1.2

        // Set up next blink (apply mood multiplier - higher = more frequent = shorter interval)
        uint32_t baseInterval = BLINK_MIN_INTERVAL + random(BLINK_MAX_INTERVAL - BLINK_MIN_INTERVAL);
        nextBlinkInterval = (uint32_t)(baseInterval / blinkRateMultiplier);
    }
}

bool IdleBehavior::shouldBlink() {
    if (blinkPending) {
        blinkPending = false;
        lastBlinkTime = millis();
        return true;
    }
    return false;
}

bool IdleBehavior::shouldYawn() {
    if (yawnPending) {
        yawnPending = false;
        yawnActive = true;
        yawnProgress = 0.0f;
        yawnStartTime = millis();
        return true;
    }
    return false;
}

void IdleBehavior::notifyActivity() {
    lastActivityTime = millis();
    yawnPending = false;

    // Reset yawn trigger with new random time
    yawnTriggerTime = YAWN_MIN_IDLE + random(YAWN_MAX_IDLE - YAWN_MIN_IDLE);

    // If currently yawning, let it finish naturally
}
