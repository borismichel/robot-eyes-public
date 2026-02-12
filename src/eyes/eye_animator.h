/**
 * @file eye_animator.h
 * @brief Composites final eye shapes from expression + gaze + blink + effects
 *
 * Owns the gaze tweeners, shape tweeners, and the shape building pipeline.
 * Produces final interpolated eye shapes ready for rendering.
 */

#ifndef EYE_ANIMATOR_H
#define EYE_ANIMATOR_H

#include <Arduino.h>
#include "eye_shape.h"
#include "../animation/tweener.h"

// Forward declarations
class ExpressionManager;
class BlinkController;
class IdleBehavior;
class ImuHandler;
class MicroExpressionSystem;
class ReactiveAnimations;
class BreathingExercise;
class SleepBehavior;

struct MoodModifiers;

class EyeAnimator {
public:
    EyeAnimator();

    void begin(ExpressionManager& em, BlinkController& blink, IdleBehavior& idle,
               ImuHandler& imu, MicroExpressionSystem& micro, ReactiveAnimations& reactive,
               BreathingExercise& breathing, SleepBehavior& sleep);

    /**
     * @brief Update gaze and build final eye shapes
     * @param dt Delta time in seconds
     * @param touching Whether screen is being touched
     * @param touchX Current touch X coordinate (screen space)
     * @param touchY Current touch Y coordinate (screen space)
     * @param lastTouchTime millis() of last touch event
     * @param isPetted Whether device is being petted
     * @param pettingPulsePhase Petting animation phase
     * @param mods Time-of-day mood modifiers
     */
    void update(float dt, bool touching, int16_t touchX, int16_t touchY,
                uint32_t lastTouchTime, bool isPetted, float pettingPulsePhase,
                const MoodModifiers& mods);

    const EyeShape& getLeftEye() const { return leftEye; }
    const EyeShape& getRightEye() const { return rightEye; }

    // Expose tweeners for ExpressionManager to set smooth time
    EyeShapeTweener& getLeftTweener() { return leftTweener; }
    EyeShapeTweener& getRightTweener() { return rightTweener; }

private:
    // Subsystem pointers
    ExpressionManager* pExprMgr;
    BlinkController* pBlink;
    IdleBehavior* pIdle;
    ImuHandler* pImu;
    MicroExpressionSystem* pMicro;
    ReactiveAnimations* pReactive;
    BreathingExercise* pBreathing;
    SleepBehavior* pSleep;

    // Gaze tweeners
    Tweener gazeX, gazeY;

    // Shape tweeners
    EyeShapeTweener leftTweener, rightTweener;

    // Target and current shapes
    EyeShape leftTarget, rightTarget;
    EyeShape leftEye, rightEye;

    void updateGaze(float dt, bool touching, int16_t touchX, int16_t touchY,
                    uint32_t lastTouchTime);
};

#endif // EYE_ANIMATOR_H
