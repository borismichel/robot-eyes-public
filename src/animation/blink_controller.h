/**
 * @file blink_controller.h
 * @brief Self-contained blink state machine
 *
 * Manages eye blink animation with support for single and double blinks.
 * Driven by IdleBehavior's blink timing.
 */

#ifndef BLINK_CONTROLLER_H
#define BLINK_CONTROLLER_H

#include <Arduino.h>

class BlinkController {
public:
    BlinkController();

    /**
     * @brief Update blink animation (call every frame)
     * @param dt Delta time in seconds (unused, uses fixed step)
     * @param shouldBlink True if idle behavior wants to trigger a blink
     * @param isDoubleBlink True for double-blink pattern
     * @param blinkSpeedMult Speed multiplier from idle behavior
     */
    void update(float dt, bool shouldBlink, bool isDoubleBlink, float blinkSpeedMult);

    /**
     * @brief Get current eye openness (1.0 = fully open, 0.0 = closed)
     */
    float getOpenness() const;

    /**
     * @brief Force trigger a blink
     */
    void trigger(bool doubleBlink = false, float speed = 1.0f);

    /**
     * @brief Reset blink state (cancel in-progress blink)
     */
    void reset();

    bool isActive() const { return blinking; }

private:
    bool blinking;
    float progress;     // 0.0 → 2.0 per blink cycle
    int count;          // Remaining blinks (1 or 2)
    float speed;        // Speed multiplier
};

#endif // BLINK_CONTROLLER_H
