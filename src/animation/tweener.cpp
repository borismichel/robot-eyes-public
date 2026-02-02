/**
 * Tweener Implementation
 * Smooth value interpolation for eye animations
 */

#include "tweener.h"
#include "../eyes/eye_shape.h"
#include <cmath>

// ============================================================================
// Easing Functions
// ============================================================================

namespace Easing {

float linear(float t) {
    return t;
}

float easeInOut(float t) {
    // Smooth cubic ease in/out
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    } else {
        float f = 2.0f * t - 2.0f;
        return 0.5f * f * f * f + 1.0f;
    }
}

float easeOut(float t) {
    // Cubic ease out - fast start, slow finish
    float f = 1.0f - t;
    return 1.0f - f * f * f;
}

float easeIn(float t) {
    // Cubic ease in - slow start, fast finish
    return t * t * t;
}

float overshoot(float t, float amount) {
    // Back ease out with overshoot
    float f = t - 1.0f;
    return f * f * ((amount + 1.0f) * f + amount) + 1.0f;
}

float smoothDamp(float current, float target, float& velocity,
                 float smoothTime, float dt, float maxSpeed) {
    // Based on Unity's SmoothDamp implementation
    // Attempt to reach target in smoothTime seconds

    smoothTime = fmax(0.0001f, smoothTime);
    float omega = 2.0f / smoothTime;

    float x = omega * dt;
    float exp_term = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

    float delta = current - target;

    // Clamp maximum speed
    float maxDelta = maxSpeed * smoothTime;
    delta = fmax(-maxDelta, fmin(delta, maxDelta));

    float temp = (velocity + omega * delta) * dt;
    velocity = (velocity - omega * temp) * exp_term;

    float result = target + (delta + temp) * exp_term;

    // Prevent overshooting
    if ((target - current > 0.0f) == (result > target)) {
        result = target;
        velocity = 0.0f;
    }

    return result;
}

} // namespace Easing

// ============================================================================
// Tweener Class
// ============================================================================

Tweener::Tweener()
    : current(0.0f)
    , target(0.0f)
    , velocity(0.0f)
    , smoothTime(0.1f)
    , easeType(EaseType::EaseInOut) {
}

Tweener::Tweener(float initialValue)
    : current(initialValue)
    , target(initialValue)
    , velocity(0.0f)
    , smoothTime(0.1f)
    , easeType(EaseType::EaseInOut) {
}

void Tweener::update(float dt) {
    if (isSettled()) {
        current = target;
        velocity = 0.0f;
        return;
    }

    current = Easing::smoothDamp(current, target, velocity, smoothTime, dt);
}

void Tweener::setTarget(float t) {
    target = t;
}

void Tweener::snapTo(float value) {
    current = value;
    target = value;
    velocity = 0.0f;
}

void Tweener::setSmoothTime(float time) {
    smoothTime = fmax(0.001f, time);
}

void Tweener::setEaseType(EaseType type) {
    easeType = type;
}

bool Tweener::isSettled(float epsilon) const {
    return fabsf(current - target) < epsilon && fabsf(velocity) < epsilon;
}

float Tweener::applyEasing(float t) const {
    switch (easeType) {
        case EaseType::Linear:
            return Easing::linear(t);
        case EaseType::EaseOut:
            return Easing::easeOut(t);
        case EaseType::EaseIn:
            return Easing::easeIn(t);
        case EaseType::Overshoot:
            return Easing::overshoot(t);
        case EaseType::EaseInOut:
        default:
            return Easing::easeInOut(t);
    }
}

// ============================================================================
// EyeShapeTweener Class
// ============================================================================

EyeShapeTweener::EyeShapeTweener()
    : width(1.0f)
    , height(1.0f)
    , cornerRadius(1.0f)
    , offsetX(0.0f)
    , offsetY(0.0f)
    , topLid(0.0f)
    , bottomLid(0.0f)
    , innerCornerY(0.0f)
    , outerCornerY(0.0f)
    , squash(1.0f)
    , stretch(1.0f)
    , openness(1.0f)
    , topPinch(0.0f)
    , bottomPinch(0.0f)
    , topCurve(0.0f)
    , bottomCurve(0.0f)
    , shapeType(ShapeType::Rectangle)
    , starPoints(5) {

    // Set default smooth times
    setSmoothTime(0.1f);
}

void EyeShapeTweener::update(float dt) {
    width.update(dt);
    height.update(dt);
    cornerRadius.update(dt);
    offsetX.update(dt);
    offsetY.update(dt);
    topLid.update(dt);
    bottomLid.update(dt);
    innerCornerY.update(dt);
    outerCornerY.update(dt);
    squash.update(dt);
    stretch.update(dt);
    openness.update(dt);
    topPinch.update(dt);
    bottomPinch.update(dt);
    topCurve.update(dt);
    bottomCurve.update(dt);
}

void EyeShapeTweener::setTarget(const EyeShape& shape) {
    width.setTarget(shape.width);
    height.setTarget(shape.height);
    cornerRadius.setTarget(shape.cornerRadius);
    offsetX.setTarget(shape.offsetX);
    offsetY.setTarget(shape.offsetY);
    topLid.setTarget(shape.topLid);
    bottomLid.setTarget(shape.bottomLid);
    innerCornerY.setTarget(shape.innerCornerY);
    outerCornerY.setTarget(shape.outerCornerY);
    squash.setTarget(shape.squash);
    stretch.setTarget(shape.stretch);
    openness.setTarget(shape.openness);
    topPinch.setTarget(shape.topPinch);
    bottomPinch.setTarget(shape.bottomPinch);
    topCurve.setTarget(shape.topCurve);
    bottomCurve.setTarget(shape.bottomCurve);
    // shapeType and starPoints snap immediately (no interpolation)
    shapeType = shape.shapeType;
    starPoints = shape.starPoints;
}

void EyeShapeTweener::getCurrentShape(EyeShape& outShape) const {
    outShape.width = width.getValue();
    outShape.height = height.getValue();
    outShape.cornerRadius = cornerRadius.getValue();
    outShape.offsetX = offsetX.getValue();
    outShape.offsetY = offsetY.getValue();
    outShape.topLid = topLid.getValue();
    outShape.bottomLid = bottomLid.getValue();
    outShape.innerCornerY = innerCornerY.getValue();
    outShape.outerCornerY = outerCornerY.getValue();
    outShape.squash = squash.getValue();
    outShape.stretch = stretch.getValue();
    outShape.openness = openness.getValue();
    outShape.topPinch = topPinch.getValue();
    outShape.bottomPinch = bottomPinch.getValue();
    outShape.topCurve = topCurve.getValue();
    outShape.bottomCurve = bottomCurve.getValue();
    outShape.shapeType = shapeType;
    outShape.starPoints = starPoints;
}

void EyeShapeTweener::setSmoothTime(float time) {
    width.setSmoothTime(time);
    height.setSmoothTime(time);
    cornerRadius.setSmoothTime(time);
    offsetX.setSmoothTime(time);
    offsetY.setSmoothTime(time);
    topLid.setSmoothTime(time);
    bottomLid.setSmoothTime(time);
    innerCornerY.setSmoothTime(time);
    outerCornerY.setSmoothTime(time);
    squash.setSmoothTime(time);
    stretch.setSmoothTime(time);
    openness.setSmoothTime(time);
    topPinch.setSmoothTime(time);
    bottomPinch.setSmoothTime(time);
    topCurve.setSmoothTime(time);
    bottomCurve.setSmoothTime(time);
}

void EyeShapeTweener::snapTo(const EyeShape& shape) {
    width.snapTo(shape.width);
    height.snapTo(shape.height);
    cornerRadius.snapTo(shape.cornerRadius);
    offsetX.snapTo(shape.offsetX);
    offsetY.snapTo(shape.offsetY);
    topLid.snapTo(shape.topLid);
    bottomLid.snapTo(shape.bottomLid);
    innerCornerY.snapTo(shape.innerCornerY);
    outerCornerY.snapTo(shape.outerCornerY);
    squash.snapTo(shape.squash);
    stretch.snapTo(shape.stretch);
    openness.snapTo(shape.openness);
    topPinch.snapTo(shape.topPinch);
    bottomPinch.snapTo(shape.bottomPinch);
    topCurve.snapTo(shape.topCurve);
    bottomCurve.snapTo(shape.bottomCurve);
    shapeType = shape.shapeType;
    starPoints = shape.starPoints;
}

bool EyeShapeTweener::isSettled() const {
    return width.isSettled() &&
           height.isSettled() &&
           cornerRadius.isSettled() &&
           offsetX.isSettled() &&
           offsetY.isSettled() &&
           topLid.isSettled() &&
           bottomLid.isSettled() &&
           innerCornerY.isSettled() &&
           outerCornerY.isSettled() &&
           squash.isSettled() &&
           stretch.isSettled() &&
           openness.isSettled() &&
           topPinch.isSettled() &&
           bottomPinch.isSettled() &&
           topCurve.isSettled() &&
           bottomCurve.isSettled();
}
