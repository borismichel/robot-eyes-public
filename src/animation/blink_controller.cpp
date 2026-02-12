/**
 * @file blink_controller.cpp
 * @brief Self-contained blink state machine implementation
 */

#include "blink_controller.h"

BlinkController::BlinkController()
    : blinking(false)
    , progress(0)
    , count(0)
    , speed(1.0f)
{
}

void BlinkController::update(float dt, bool shouldBlink, bool isDoubleBlink, float blinkSpeedMult) {
    if (shouldBlink && !blinking) {
        trigger(isDoubleBlink, blinkSpeedMult);
    }

    if (blinking) {
        progress += 0.48f * speed;  // Doubled blink speed
        if (progress >= 2.0f) {
            count--;
            if (count > 0) {
                // More blinks to do (double-blink)
                progress = 0;
            } else {
                // Done blinking
                blinking = false;
                progress = 0;
            }
        }
    }
}

float BlinkController::getOpenness() const {
    if (!blinking) return 1.0f;

    // Hold fully closed when near midpoint (ensures full closure with fast animation)
    if (progress >= 0.85f && progress <= 1.15f) {
        return 0.0f;  // Fully closed
    }

    if (progress < 0.85f) {
        // Closing phase: scale 0-0.85 to openness 1.0-0.0
        return 1.0f - (progress / 0.85f);
    } else {
        // Opening phase: scale 1.15-2.0 to openness 0.0-1.0
        return (progress - 1.15f) / 0.85f;
    }
}

void BlinkController::trigger(bool doubleBlink, float blinkSpeedMult) {
    if (!blinking) {
        blinking = true;
        progress = 0;
        count = doubleBlink ? 2 : 1;
        speed = blinkSpeedMult;
    }
}

void BlinkController::reset() {
    blinking = false;
    progress = 0;
    count = 0;
}
