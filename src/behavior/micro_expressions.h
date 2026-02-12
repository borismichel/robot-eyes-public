/**
 * @file micro_expressions.h
 * @brief Idle personality micro-expression system
 *
 * Random idle personality moments: expression-based (curious, thinking, etc.)
 * and animation-based (wink, eye roll, shifty eyes, etc.)
 */

#ifndef MICRO_EXPRESSIONS_H
#define MICRO_EXPRESSIONS_H

#include <Arduino.h>
#include "expression_manager.h"

enum class MicroExpressionType {
    None,
    // Expression-based (uses Expression enum)
    CuriousGlance, ThinkingMoment, ContentSmile, MischievousLook,
    BoredGlance, AlertPerk, SadMoment, SurprisedLook, AngryFlash,
    GrumpyMood, FocusedStare, ConfusedGlance, SmugGrin, DreamyGaze,
    SkepticalLook, SquintPeer,
    // Animation-based (custom gaze/blink patterns)
    Wink, EyeRoll, DoubleTake, ShiftyEyes, QuickSigh
};

class MicroExpressionSystem {
public:
    MicroExpressionSystem();

    /**
     * @brief Initialize with reference to expression manager
     */
    void begin(ExpressionManager& em);

    /**
     * @brief Trigger a random micro-expression
     */
    void trigger();

    /**
     * @brief Update animation state (call every frame)
     * @param dt Delta time in seconds
     * @param now Current millis()
     * @return true if micro-expression ended this frame
     */
    bool update(float dt, uint32_t now);

    /**
     * @brief Cancel any active micro-expression
     */
    void cancel();

    /**
     * @brief Check if micro-expression is active
     */
    bool isActive() const { return active; }

    /**
     * @brief Check if it's time to trigger a new micro-expression
     */
    bool shouldTrigger(uint32_t now) const { return !active && now >= nextTriggerTime; }

    /**
     * @brief Schedule next trigger time
     */
    void scheduleNext(uint32_t now);

    /**
     * @brief Get gaze offset for animation-based micro-expressions
     */
    void getGazeOffset(float& x, float& y) const;

    /**
     * @brief Get openness modifier for wink/sigh animations
     */
    float getOpenness(bool isLeftEye) const;

    /**
     * @brief Get the current micro-expression type
     */
    MicroExpressionType getCurrentType() const { return currentType; }

private:
    ExpressionManager* pExprMgr;
    uint8_t exprToken;

    bool active;
    MicroExpressionType currentType;
    uint32_t startTime;
    uint32_t nextTriggerTime;
    float phase;            // 0.0-1.0 animation progress

    // Wink state
    bool winkLeftEye;
    float winkProgress;

    uint32_t getDuration() const;
    Expression getExpressionForType(MicroExpressionType type) const;

    // Timing constants
    static const uint32_t MIN_INTERVAL = 2 * 60 * 1000;    // 2 minutes
    static const uint32_t MAX_INTERVAL = 3 * 60 * 1000;    // 3 minutes
    static const uint32_t DURATION_SHORT = 800;
    static const uint32_t DURATION_MEDIUM = 1500;
    static const uint32_t DURATION_LONG = 2500;
    static const uint32_t DURATION_MOOD_MIN = 60 * 1000;
    static const uint32_t DURATION_MOOD_MAX = 180 * 1000;
};

#endif // MICRO_EXPRESSIONS_H
