/**
 * @file eye_animator.cpp
 * @brief Eye shape compositor implementation
 */

#include "eye_animator.h"
#include "../behavior/expression_manager.h"
#include "../animation/blink_controller.h"
#include "../behavior/idle_behavior.h"
#include "../input/imu_handler.h"
#include "../behavior/micro_expressions.h"
#include "../behavior/reactive_animations.h"
#include "../behavior/breathing_exercise.h"
#include "../behavior/sleep_behavior.h"
#include "../behavior/time_mood.h"

#define SCREEN_WIDTH  368
#define SCREEN_HEIGHT 448

EyeAnimator::EyeAnimator()
    : pExprMgr(nullptr)
    , pBlink(nullptr)
    , pIdle(nullptr)
    , pImu(nullptr)
    , pMicro(nullptr)
    , pReactive(nullptr)
    , pBreathing(nullptr)
    , pSleep(nullptr)
{
}

void EyeAnimator::begin(ExpressionManager& em, BlinkController& blink, IdleBehavior& idle,
                         ImuHandler& imu, MicroExpressionSystem& micro, ReactiveAnimations& reactive,
                         BreathingExercise& breathing, SleepBehavior& sleep) {
    pExprMgr = &em;
    pBlink = &blink;
    pIdle = &idle;
    pImu = &imu;
    pMicro = &micro;
    pReactive = &reactive;
    pBreathing = &breathing;
    pSleep = &sleep;

    gazeX.setSmoothTime(0.15f);
    gazeY.setSmoothTime(0.15f);
}

void EyeAnimator::updateGaze(float dt, bool touching, int16_t touchX, int16_t touchY,
                              uint32_t lastTouchTime) {
    if (touching) {
        float targetX = (touchX - SCREEN_WIDTH / 2) / (float)(SCREEN_WIDTH / 2);
        float targetY = (touchY - SCREEN_HEIGHT / 2) / (float)(SCREEN_HEIGHT / 2);
        gazeX.setTarget(constrain(targetX, -1.0f, 1.0f));
        gazeY.setTarget(constrain(targetY, -1.0f, 1.0f));
    } else if (millis() - lastTouchTime > 500) {
        gazeX.setTarget(pIdle->getIdleGazeX());
        gazeY.setTarget(pIdle->getIdleGazeY());
    }

    gazeX.update(dt);
    gazeY.update(dt);
}

void EyeAnimator::update(float dt, bool touching, int16_t touchX, int16_t touchY,
                          uint32_t lastTouchTime, bool isPetted, float pettingPulsePhase,
                          const MoodModifiers& mods) {
    // Update gaze (disabled during breathing exercise for focus)
    if (!pBreathing->isActive()) {
        updateGaze(dt, touching, touchX, touchY, lastTouchTime);
    }

    // Build target shapes from base expression + dynamic state
    float openness = pBlink->getOpenness();

    // Get combined gaze (touch target or idle scanning) + micro-movement + tilt
    float totalGazeX = gazeX.getValue() + pIdle->getMicroX();
    float totalGazeY = gazeY.getValue() + pIdle->getMicroY();

    // Add tilt-based gaze if enabled and not touching
    if (pImu->isTiltGazeEnabled() && !touching) {
        totalGazeX += pImu->getTiltGazeY() * 0.5f;
        totalGazeY += pImu->getTiltGazeX() * 0.5f;
    }

    // Copy base expression and apply dynamic parameters
    leftTarget = pExprMgr->getLeftBase();
    leftTarget.openness *= openness;
    leftTarget.offsetX += totalGazeX;
    leftTarget.offsetY += totalGazeY;

    rightTarget = pExprMgr->getRightBase();
    rightTarget.openness *= openness;
    rightTarget.offsetX += totalGazeX;
    rightTarget.offsetY += totalGazeY;

    // Override with breathing exercise shapes when active
    if (pBreathing->isActive()) {
        pBreathing->getTargetShapes(leftTarget, rightTarget);
    } else {
        // Apply all other eye modifications only when NOT in breathing exercise

        // Petting bobbing effect
        if (isPetted) {
            float bobAmount = 0.1f * sinf(pettingPulsePhase * 2.0f * PI);
            leftTarget.topLid += bobAmount;
            rightTarget.topLid += bobAmount;
        }

        // Time-of-day mood lid offset
        if (mods.baseLidOffset > 0.0f && !isPetted && !pSleep->isDrowsy()) {
            leftTarget.topLid += mods.baseLidOffset;
            rightTarget.topLid += mods.baseLidOffset;
        }

        // Side-looking squint effect
        float squintAmount = totalGazeY * 0.25f;
        leftTarget.height *= (1.0f + squintAmount);
        rightTarget.height *= (1.0f - squintAmount);
        if (squintAmount > 0.1f) {
            rightTarget.topLid += squintAmount * 0.3f;
        } else if (squintAmount < -0.1f) {
            leftTarget.topLid += (-squintAmount) * 0.3f;
        }

        // Micro-expression animation effects
        if (pMicro->isActive()) {
            float microGazeX = 0, microGazeY = 0;
            pMicro->getGazeOffset(microGazeX, microGazeY);
            leftTarget.offsetX += microGazeX;
            leftTarget.offsetY += microGazeY;
            rightTarget.offsetX += microGazeX;
            rightTarget.offsetY += microGazeY;

            float leftOpenness = pMicro->getOpenness(true);
            float rightOpenness = pMicro->getOpenness(false);
            leftTarget.openness *= leftOpenness;
            rightTarget.openness *= rightOpenness;
        }
    }

    // Apply tweeners
    leftTweener.setTarget(leftTarget);
    rightTweener.setTarget(rightTarget);
    leftTweener.update(dt);
    rightTweener.update(dt);

    // Get current interpolated shapes
    leftTweener.getCurrentShape(leftEye);
    rightTweener.getCurrentShape(rightEye);

    // Apply animation phase for special shapes (stars, hearts)
    leftEye.animPhase = pReactive->getShapeAnimPhase();
    rightEye.animPhase = pReactive->getShapeAnimPhase();

    // Pulse hearts when showing love
    if (pReactive->isLove()) {
        float pulseScale = 1.0f + 0.15f * sinf(pReactive->getShapeAnimPhase() * 4.0f * PI);
        leftEye.height *= pulseScale;
        rightEye.height *= pulseScale;
    }
}
