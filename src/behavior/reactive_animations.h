/**
 * @file reactive_animations.h
 * @brief Timed reactive expression animations
 *
 * Manages all timed expression reactions: IMU events, irritated (loud noise),
 * love hearts (after petting), random joy, orientation changes, and
 * breathing post-completion states.
 */

#ifndef REACTIVE_ANIMATIONS_H
#define REACTIVE_ANIMATIONS_H

#include <Arduino.h>
#include "expression_manager.h"
#include "../input/imu_handler.h"

// Forward declarations
class MoodDrift;
class AudioPlayer;

class ReactiveAnimations {
public:
    ReactiveAnimations();
    void begin(ExpressionManager& em, MoodDrift& mood, AudioPlayer& audio);

    /**
     * @brief Update all timers (call every frame)
     */
    void update(float dt, uint32_t now);

    // --- IMU reactions ---
    void triggerImuReaction(ImuEvent event);
    void triggerYawn();
    bool isImuReacting() const { return imuReacting; }

    // --- Irritated (loud noise) ---
    void triggerIrritated(uint32_t now);
    bool isIrritated() const { return showingIrritated; }

    // --- Love hearts (after petting) ---
    void triggerLove(uint32_t now);
    bool isLove() const { return showingLove; }

    // --- Joy (random happy moments) ---
    void triggerJoy(uint32_t now);
    void cancelJoy();
    bool isJoy() const { return showingJoy; }
    bool shouldTriggerJoy(uint32_t now) const;
    void scheduleNextJoy(uint32_t now);

    // --- Orientation reactions ---
    void orientationChanged(Orientation orient, bool isPetted);

    // --- Breathing post-completion ---
    void notifyBreathingComplete(uint32_t now, bool soundEnabled);
    bool isBreathingContent() const { return breathingContentUntil > 0; }
    bool isBreathingRelaxed() const { return breathingRelaxedUntil > 0; }

    // --- Animation phases ---
    float getBouncePhase() const { return bouncePhase; }
    void addBounce(float dt) { bouncePhase += dt * 3.0f; }
    void resetBounce() { bouncePhase = 0.0f; }
    float getShapeAnimPhase() const { return shapeAnimPhase; }

    // --- Compound queries ---
    bool isAnyActive() const;
    bool isOrientationActive() const { return orientationToken != EXPR_TOKEN_INVALID; }

private:
    ExpressionManager* pExprMgr;
    MoodDrift* pMoodDrift;
    AudioPlayer* pAudioPlayer;

    // IMU reaction state
    bool imuReacting;
    uint32_t imuReactionStart;
    uint8_t imuToken;
    static const uint32_t IMU_REACTION_DURATION = 4000;

    // Irritated state
    bool showingIrritated;
    uint32_t irritatedStart;
    uint8_t irritatedToken;
    static const uint32_t IRRITATED_DURATION = 3000;

    // Love state
    bool showingLove;
    uint32_t loveStart;
    uint8_t loveToken;
    static const uint32_t LOVE_DURATION = 4000;

    // Joy state
    bool showingJoy;
    uint32_t joyStart;
    uint32_t nextJoyTime;
    uint8_t joyToken;
    static const uint32_t JOY_DURATION = 3000;
    static const uint32_t JOY_MIN_INTERVAL = 10 * 60 * 1000;
    static const uint32_t JOY_MAX_INTERVAL = 30 * 60 * 1000;

    // Orientation
    Orientation lastOrientation;
    uint8_t orientationToken;

    // Breathing post-completion
    bool breathingCompletePending;
    bool breathingSoundEnabled;
    uint32_t breathingCompleteTime;
    uint32_t breathingContentUntil;
    uint32_t breathingRelaxedUntil;
    uint8_t breathingToken;

    // Animation phases
    float bouncePhase;     // Shared bounce animation (joy, content, petting)
    float shapeAnimPhase;  // Rotating stars/shapes
};

#endif // REACTIVE_ANIMATIONS_H
