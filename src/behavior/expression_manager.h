/**
 * @file expression_manager.h
 * @brief Priority-based expression control
 *
 * Replaces manual save/restore pattern (8 expressionBefore* globals)
 * with a priority stack. Subsystems request(expr, priority) → get token,
 * release(token) → auto-reverts to next-highest active expression.
 */

#ifndef EXPRESSION_MANAGER_H
#define EXPRESSION_MANAGER_H

#include <Arduino.h>
#include "expressions.h"
#include "../eyes/eye_shape.h"
#include "../animation/tweener.h"

/// Priority levels for expression control (highest wins)
enum class ExprPriority : uint8_t {
    Base = 0,      ///< Mood drift base expression
    MicroExpr,     ///< Idle personality micro-expressions
    Timer,         ///< Pomodoro/countdown expression effects
    Behavior,      ///< Joy, love, irritated, orientation, breathing post-completion
    Gesture,       ///< Petting (ContentPetting)
    IMU,           ///< Scared, dazed, dizzy, yawn
    Assistant,     ///< Voice assistant state
    Sleep          ///< Highest — sleep/wake overrides all
};

/// Max concurrent expression slots
#define EXPR_MAX_SLOTS 16

/// Invalid token sentinel
#define EXPR_TOKEN_INVALID 0xFF

/**
 * @class ExpressionManager
 * @brief Manages expression priorities with automatic fallback
 */
class ExpressionManager {
public:
    ExpressionManager();

    /**
     * @brief Initialize with references to eye shape tweeners
     */
    void begin(EyeShapeTweener& leftTweener, EyeShapeTweener& rightTweener);

    /**
     * @brief Request an expression at a given priority level
     * @return Token to later update or release; EXPR_TOKEN_INVALID on failure
     */
    uint8_t request(Expression expr, ExprPriority priority);

    /**
     * @brief Update the expression of an existing request
     * No-op if token is invalid or inactive
     */
    void update(uint8_t token, Expression expr);

    /**
     * @brief Release a previously requested expression
     * Auto-reverts to next-highest priority active expression
     */
    void release(uint8_t token);

    /**
     * @brief Get the currently active expression
     */
    Expression getCurrent() const { return activeExpression; }

    /**
     * @brief Get the current left eye base shape
     */
    const EyeShape& getLeftBase() const { return leftBase; }

    /**
     * @brief Get the current right eye base shape
     */
    const EyeShape& getRightBase() const { return rightBase; }

    /**
     * @brief Milliseconds since last expression change
     */
    uint32_t getTimeSinceChange() const { return millis() - lastChangeTime; }

    /**
     * @brief Check if a token is currently active
     */
    bool isActive(uint8_t token) const;

    /**
     * @brief Get display name of current expression
     */
    const char* getCurrentName() const { return getExpressionName(activeExpression); }

private:
    struct Slot {
        Expression expr;
        ExprPriority priority;
        uint32_t requestTime;
        bool active;
    };

    Slot slots[EXPR_MAX_SLOTS];
    Expression activeExpression;
    EyeShape leftBase;
    EyeShape rightBase;
    uint32_t lastChangeTime;

    EyeShapeTweener* pLeftTweener;
    EyeShapeTweener* pRightTweener;

    void applyHighestPriority();
    void applyExpression(Expression expr);
    static float getTransitionTime(Expression expr);
};

#endif // EXPRESSION_MANAGER_H
