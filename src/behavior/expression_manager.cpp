/**
 * @file expression_manager.cpp
 * @brief Priority-based expression control implementation
 */

#include "expression_manager.h"

ExpressionManager::ExpressionManager()
    : activeExpression(Expression::Neutral)
    , lastChangeTime(0)
    , pLeftTweener(nullptr)
    , pRightTweener(nullptr)
{
    leftBase = getExpressionShape(Expression::Neutral, true);
    rightBase = getExpressionShape(Expression::Neutral, false);
    for (int i = 0; i < EXPR_MAX_SLOTS; i++) {
        slots[i].active = false;
    }
}

void ExpressionManager::begin(EyeShapeTweener& leftTweener, EyeShapeTweener& rightTweener) {
    pLeftTweener = &leftTweener;
    pRightTweener = &rightTweener;
}

uint8_t ExpressionManager::request(Expression expr, ExprPriority priority) {
    // Find a free slot
    for (uint8_t i = 0; i < EXPR_MAX_SLOTS; i++) {
        if (!slots[i].active) {
            slots[i].expr = expr;
            slots[i].priority = priority;
            slots[i].requestTime = millis();
            slots[i].active = true;
            applyHighestPriority();
            return i;
        }
    }
    Serial.println("[ExprMgr] No free slots!");
    return EXPR_TOKEN_INVALID;
}

void ExpressionManager::update(uint8_t token, Expression expr) {
    if (token >= EXPR_MAX_SLOTS || !slots[token].active) return;
    slots[token].expr = expr;
    slots[token].requestTime = millis();
    applyHighestPriority();
}

void ExpressionManager::release(uint8_t token) {
    if (token >= EXPR_MAX_SLOTS || !slots[token].active) return;
    slots[token].active = false;
    applyHighestPriority();
}

bool ExpressionManager::isActive(uint8_t token) const {
    if (token >= EXPR_MAX_SLOTS) return false;
    return slots[token].active;
}

void ExpressionManager::applyHighestPriority() {
    // Find highest priority active slot (ties broken by most recent request)
    int bestIdx = -1;
    for (int i = 0; i < EXPR_MAX_SLOTS; i++) {
        if (!slots[i].active) continue;
        if (bestIdx < 0) {
            bestIdx = i;
        } else if ((uint8_t)slots[i].priority > (uint8_t)slots[bestIdx].priority) {
            bestIdx = i;
        } else if ((uint8_t)slots[i].priority == (uint8_t)slots[bestIdx].priority &&
                   slots[i].requestTime > slots[bestIdx].requestTime) {
            bestIdx = i;
        }
    }

    Expression newExpr = (bestIdx >= 0) ? slots[bestIdx].expr : Expression::Neutral;
    if (newExpr != activeExpression) {
        applyExpression(newExpr);
    }
}

void ExpressionManager::applyExpression(Expression expr) {
    activeExpression = expr;
    leftBase = getExpressionShape(expr, true);
    rightBase = getExpressionShape(expr, false);
    lastChangeTime = millis();

    float smoothTime = getTransitionTime(expr);
    if (pLeftTweener) pLeftTweener->setSmoothTime(smoothTime);
    if (pRightTweener) pRightTweener->setSmoothTime(smoothTime);

    Serial.printf("Expression: %s (%.2fs)\n", getExpressionName(expr), smoothTime);
}

float ExpressionManager::getTransitionTime(Expression expr) {
    switch (expr) {
        // Fast snap for sudden reactions
        case Expression::Startled:
        case Expression::Scared:
        case Expression::Alert:
            return 0.08f;

        // Quick but not instant for surprise/joy
        case Expression::Surprised:
        case Expression::Joy:
        case Expression::Joyful:
        case Expression::Wink:
            return 0.12f;

        // Slow, heavy transitions for tired/sad emotions
        case Expression::Sad:
        case Expression::Sleepy:
        case Expression::Bored:
        case Expression::Yawn:
            return 0.35f;

        // Medium-slow for relaxed states
        case Expression::Content:
        case Expression::ContentPetting:
        case Expression::Dreamy:
        case Expression::Love:
            return 0.25f;

        // Default timing for other expressions
        default:
            return 0.2f;
    }
}
