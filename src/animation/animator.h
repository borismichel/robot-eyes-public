/**
 * Animator - Handles smooth interpolation between eye states
 */

#ifndef ANIMATOR_H
#define ANIMATOR_H

#include <Arduino.h>
#include "../eyes/eye_params.h"
#include "../eyes/expressions.h"

/**
 * Easing functions for smooth animations
 */
enum class EaseType {
    LINEAR,
    EASE_IN,
    EASE_OUT,
    EASE_IN_OUT,
    BOUNCE,
    ELASTIC
};

/**
 * Apply easing function to progress value
 */
float apply_easing(float t, EaseType ease);

/**
 * Animation state for a single transition
 */
struct Animation {
    EyeParams start_left;
    EyeParams start_right;
    EyeParams target_left;
    EyeParams target_right;

    uint32_t start_time;
    uint32_t duration;
    EaseType easing;

    bool active;

    Animation()
        : start_time(0)
        , duration(0)
        , easing(EaseType::EASE_IN_OUT)
        , active(false) {}
};

/**
 * Animator class - manages expression transitions
 */
class Animator {
public:
    Animator();

    /**
     * Set current eye parameters immediately (no animation)
     */
    void set_immediate(const Expression& expr);

    /**
     * Animate to target expression
     * @param expr Target expression
     * @param duration_ms Animation duration in milliseconds
     * @param ease Easing function
     */
    void animate_to(const Expression& expr, uint32_t duration_ms, EaseType ease = EaseType::EASE_IN_OUT);

    /**
     * Animate only the left eye
     */
    void animate_left(const EyeParams& params, uint32_t duration_ms, EaseType ease = EaseType::EASE_IN_OUT);

    /**
     * Animate only the right eye
     */
    void animate_right(const EyeParams& params, uint32_t duration_ms, EaseType ease = EaseType::EASE_IN_OUT);

    /**
     * Update animation state (call every frame)
     * @return true if animation is still in progress
     */
    bool update();

    /**
     * Check if any animation is active
     */
    bool is_animating() const { return m_main_anim.active; }

    /**
     * Get current interpolated eye parameters
     */
    const EyeParams& current_left() const { return m_current_left; }
    const EyeParams& current_right() const { return m_current_right; }

    /**
     * Stop current animation
     */
    void stop();

private:
    Animation m_main_anim;
    EyeParams m_current_left;
    EyeParams m_current_right;
};

#endif // ANIMATOR_H
