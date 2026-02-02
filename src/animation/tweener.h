/**
 * Tweener - Smooth value interpolation with various easing functions
 * Used for animating eye parameters smoothly
 */

#ifndef TWEENER_H
#define TWEENER_H

#include <Arduino.h>
#include "../eyes/eye_shape.h"

// Easing function types
enum class EaseType {
    Linear,
    EaseInOut,    // Smooth start and end (default for most transitions)
    EaseOut,      // Fast start, slow end (for startle responses)
    EaseIn,       // Slow start, fast end (for falling asleep)
    Overshoot     // Bouncy overshoot (for happy animations)
};

/**
 * Tweener class - smoothly interpolates a single float value
 * Uses smooth damp algorithm for natural-feeling motion
 */
class Tweener {
public:
    Tweener();
    Tweener(float initialValue);

    /**
     * Update the tweener (call every frame)
     * @param dt Delta time in seconds
     */
    void update(float dt);

    /**
     * Set the target value to tween towards
     */
    void setTarget(float target);

    /**
     * Immediately snap to a value (no animation)
     */
    void snapTo(float value);

    /**
     * Set the smooth time (how long to reach target)
     * @param time Time in seconds (default 0.1)
     */
    void setSmoothTime(float time);

    /**
     * Set the easing function
     */
    void setEaseType(EaseType type);

    /**
     * Get current value
     */
    float getValue() const { return current; }

    /**
     * Get target value
     */
    float getTarget() const { return target; }

    /**
     * Check if value has settled (within epsilon of target)
     */
    bool isSettled(float epsilon = 0.001f) const;

private:
    float current;
    float target;
    float velocity;
    float smoothTime;
    EaseType easeType;

    // Apply easing based on progress
    float applyEasing(float t) const;
};

/**
 * EyeShapeTweener - Tweens all parameters of an EyeShape
 */
class EyeShapeTweener {
public:
    EyeShapeTweener();

    /**
     * Update all tweeners (call every frame)
     * @param dt Delta time in seconds
     */
    void update(float dt);

    /**
     * Set target shape to tween towards
     */
    void setTarget(const EyeShape& shape);

    /**
     * Get current interpolated shape
     */
    void getCurrentShape(EyeShape& outShape) const;

    /**
     * Set smooth time for all parameters
     */
    void setSmoothTime(float time);

    /**
     * Immediately snap to shape (no animation)
     */
    void snapTo(const EyeShape& shape);

    /**
     * Check if all parameters have settled
     */
    bool isSettled() const;

private:
    Tweener width;
    Tweener height;
    Tweener cornerRadius;
    Tweener offsetX;
    Tweener offsetY;
    Tweener topLid;
    Tweener bottomLid;
    Tweener innerCornerY;
    Tweener outerCornerY;
    Tweener squash;
    Tweener stretch;
    Tweener openness;
    Tweener topPinch;
    Tweener bottomPinch;
    Tweener topCurve;
    Tweener bottomCurve;

    // Non-interpolated fields (snap immediately)
    ShapeType shapeType;
    int starPoints;
};

// Easing utility functions (can be used standalone)
namespace Easing {
    float linear(float t);
    float easeInOut(float t);
    float easeOut(float t);
    float easeIn(float t);
    float overshoot(float t, float amount = 1.70158f);

    // Smooth damp algorithm (Unity-style)
    float smoothDamp(float current, float target, float& velocity,
                     float smoothTime, float dt, float maxSpeed = 1000.0f);
}

#endif // TWEENER_H
