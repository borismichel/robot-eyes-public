/**
 * @file micro_expressions.cpp
 * @brief Idle personality micro-expression system implementation
 */

#include "micro_expressions.h"
#include <math.h>

MicroExpressionSystem::MicroExpressionSystem()
    : pExprMgr(nullptr)
    , exprToken(EXPR_TOKEN_INVALID)
    , active(false)
    , currentType(MicroExpressionType::None)
    , startTime(0)
    , nextTriggerTime(0)
    , phase(0.0f)
    , winkLeftEye(true)
    , winkProgress(0.0f)
{
}

void MicroExpressionSystem::begin(ExpressionManager& em) {
    pExprMgr = &em;
    scheduleNext(millis());
}

void MicroExpressionSystem::scheduleNext(uint32_t now) {
    nextTriggerTime = now + MIN_INTERVAL + random(MAX_INTERVAL - MIN_INTERVAL);
}

void MicroExpressionSystem::trigger() {
    // Pick a random micro-expression type (weighted distribution)
    int r = random(200);

    if (r < 14)       currentType = MicroExpressionType::CuriousGlance;
    else if (r < 26)  currentType = MicroExpressionType::ThinkingMoment;
    else if (r < 38)  currentType = MicroExpressionType::ContentSmile;
    else if (r < 48)  currentType = MicroExpressionType::MischievousLook;
    else if (r < 58)  currentType = MicroExpressionType::BoredGlance;
    else if (r < 68)  currentType = MicroExpressionType::AlertPerk;
    else if (r < 76)  currentType = MicroExpressionType::SadMoment;
    else if (r < 84)  currentType = MicroExpressionType::SurprisedLook;
    else if (r < 91)  currentType = MicroExpressionType::AngryFlash;
    else if (r < 98)  currentType = MicroExpressionType::GrumpyMood;
    else if (r < 106) currentType = MicroExpressionType::FocusedStare;
    else if (r < 114) currentType = MicroExpressionType::ConfusedGlance;
    else if (r < 122) currentType = MicroExpressionType::SmugGrin;
    else if (r < 130) currentType = MicroExpressionType::DreamyGaze;
    else if (r < 138) currentType = MicroExpressionType::SkepticalLook;
    else if (r < 146) currentType = MicroExpressionType::SquintPeer;
    else if (r < 156) {
        currentType = MicroExpressionType::Wink;
        winkLeftEye = random(2) == 0;
    }
    else if (r < 166) currentType = MicroExpressionType::DoubleTake;
    else if (r < 178) currentType = MicroExpressionType::ShiftyEyes;
    else if (r < 192) currentType = MicroExpressionType::QuickSigh;
    else              currentType = MicroExpressionType::EyeRoll;

    active = true;
    startTime = millis();
    phase = 0.0f;
    winkProgress = 0.0f;

    // Request expression for expression-based types
    Expression expr = getExpressionForType(currentType);
    if (expr != Expression::Neutral && pExprMgr) {
        exprToken = pExprMgr->request(expr, ExprPriority::MicroExpr);
    }

    Serial.printf("Micro-expression: %d\n", (int)currentType);
}

bool MicroExpressionSystem::update(float dt, uint32_t now) {
    if (!active) return false;

    uint32_t duration = getDuration();
    uint32_t elapsed = now - startTime;
    phase = (float)elapsed / (float)duration;

    if (elapsed >= duration) {
        cancel();
        Serial.println("Micro-expression done");
        return true;
    }
    return false;
}

void MicroExpressionSystem::cancel() {
    active = false;
    currentType = MicroExpressionType::None;
    if (pExprMgr && pExprMgr->isActive(exprToken)) {
        pExprMgr->release(exprToken);
    }
    exprToken = EXPR_TOKEN_INVALID;
}

void MicroExpressionSystem::getGazeOffset(float& offsetX, float& offsetY) const {
    offsetX = 0;
    offsetY = 0;

    switch (currentType) {
        case MicroExpressionType::DoubleTake:
            if (phase < 0.3f) {
                offsetY = phase / 0.3f * 0.5f;
            } else if (phase < 0.5f) {
                offsetY = 0.5f;
            } else {
                offsetY = 0.5f * (1.0f - (phase - 0.5f) / 0.5f);
            }
            break;

        case MicroExpressionType::ShiftyEyes: {
            float cycle = fmodf(phase * 4.0f, 1.0f);
            offsetY = sinf(cycle * 2.0f * M_PI) * 0.4f;
            break;
        }

        case MicroExpressionType::EyeRoll: {
            float angle = phase * 2.0f * M_PI;
            offsetX = -sinf(angle) * 0.35f;
            offsetY = cosf(angle) * 0.35f;
            break;
        }

        default:
            break;
    }
}

float MicroExpressionSystem::getOpenness(bool isLeftEye) const {
    switch (currentType) {
        case MicroExpressionType::Wink:
            if ((isLeftEye && winkLeftEye) || (!isLeftEye && !winkLeftEye)) {
                if (phase < 0.3f) {
                    return 1.0f - (phase / 0.3f);
                } else if (phase < 0.6f) {
                    return 0.0f;
                } else {
                    return (phase - 0.6f) / 0.4f;
                }
            }
            return 1.0f;

        case MicroExpressionType::QuickSigh:
            if (phase < 0.25f) {
                return 1.0f - (phase / 0.25f) * 0.7f;
            } else if (phase < 0.5f) {
                return 0.3f;
            } else {
                float openPhase = (phase - 0.5f) / 0.5f;
                return 0.3f + openPhase * 0.8f;
            }

        default:
            return 1.0f;
    }
}

uint32_t MicroExpressionSystem::getDuration() const {
    switch (currentType) {
        case MicroExpressionType::CuriousGlance:
        case MicroExpressionType::AlertPerk:
        case MicroExpressionType::QuickSigh:
        case MicroExpressionType::SurprisedLook:
        case MicroExpressionType::AngryFlash:
            return DURATION_SHORT;
        case MicroExpressionType::ThinkingMoment:
        case MicroExpressionType::ContentSmile:
        case MicroExpressionType::MischievousLook:
        case MicroExpressionType::BoredGlance:
        case MicroExpressionType::Wink:
        case MicroExpressionType::DoubleTake:
        case MicroExpressionType::ConfusedGlance:
        case MicroExpressionType::SmugGrin:
        case MicroExpressionType::SkepticalLook:
        case MicroExpressionType::SquintPeer:
            return DURATION_MEDIUM;
        case MicroExpressionType::EyeRoll:
        case MicroExpressionType::ShiftyEyes:
            return DURATION_LONG;
        case MicroExpressionType::GrumpyMood:
        case MicroExpressionType::FocusedStare:
        case MicroExpressionType::DreamyGaze:
        case MicroExpressionType::SadMoment:
            return DURATION_MOOD_MIN + random(DURATION_MOOD_MAX - DURATION_MOOD_MIN);
        default:
            return DURATION_SHORT;
    }
}

Expression MicroExpressionSystem::getExpressionForType(MicroExpressionType type) const {
    switch (type) {
        case MicroExpressionType::CuriousGlance:   return Expression::Curious;
        case MicroExpressionType::ThinkingMoment:   return Expression::Thinking;
        case MicroExpressionType::ContentSmile:     return Expression::Happy;
        case MicroExpressionType::MischievousLook:  return Expression::Mischievous;
        case MicroExpressionType::BoredGlance:      return Expression::Bored;
        case MicroExpressionType::AlertPerk:        return Expression::Alert;
        case MicroExpressionType::SadMoment:        return Expression::Sad;
        case MicroExpressionType::SurprisedLook:    return Expression::Surprised;
        case MicroExpressionType::AngryFlash:       return Expression::Angry;
        case MicroExpressionType::GrumpyMood:       return Expression::Grumpy;
        case MicroExpressionType::FocusedStare:     return Expression::Focused;
        case MicroExpressionType::ConfusedGlance:   return Expression::Confused;
        case MicroExpressionType::SmugGrin:         return Expression::Smug;
        case MicroExpressionType::DreamyGaze:       return Expression::Dreamy;
        case MicroExpressionType::SkepticalLook:    return Expression::Skeptical;
        case MicroExpressionType::SquintPeer:       return Expression::Squint;
        default:                                    return Expression::Neutral;
    }
}
