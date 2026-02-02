/**
 * Animator Implementation
 */

#include "animator.h"
#include <cmath>

float apply_easing(float t, EaseType ease) {
    // Clamp t to 0-1
    t = constrain(t, 0.0f, 1.0f);

    switch (ease) {
        case EaseType::LINEAR:
            return t;

        case EaseType::EASE_IN:
            return t * t;

        case EaseType::EASE_OUT:
            return 1.0f - (1.0f - t) * (1.0f - t);

        case EaseType::EASE_IN_OUT:
            if (t < 0.5f) {
                return 2.0f * t * t;
            } else {
                return 1.0f - powf(-2.0f * t + 2.0f, 2.0f) / 2.0f;
            }

        case EaseType::BOUNCE: {
            float n1 = 7.5625f;
            float d1 = 2.75f;
            if (t < 1.0f / d1) {
                return n1 * t * t;
            } else if (t < 2.0f / d1) {
                t -= 1.5f / d1;
                return n1 * t * t + 0.75f;
            } else if (t < 2.5f / d1) {
                t -= 2.25f / d1;
                return n1 * t * t + 0.9375f;
            } else {
                t -= 2.625f / d1;
                return n1 * t * t + 0.984375f;
            }
        }

        case EaseType::ELASTIC: {
            if (t == 0.0f || t == 1.0f) return t;
            float c4 = (2.0f * M_PI) / 3.0f;
            return powf(2.0f, -10.0f * t) * sinf((t * 10.0f - 0.75f) * c4) + 1.0f;
        }

        default:
            return t;
    }
}

Animator::Animator() {
    m_current_left = EyeParams::neutral();
    m_current_right = EyeParams::neutral();
}

void Animator::set_immediate(const Expression& expr) {
    m_main_anim.active = false;
    m_current_left = expr.left;
    m_current_right = expr.right;
}

void Animator::animate_to(const Expression& expr, uint32_t duration_ms, EaseType ease) {
    m_main_anim.start_left = m_current_left;
    m_main_anim.start_right = m_current_right;
    m_main_anim.target_left = expr.left;
    m_main_anim.target_right = expr.right;
    m_main_anim.start_time = millis();
    m_main_anim.duration = duration_ms;
    m_main_anim.easing = ease;
    m_main_anim.active = true;
}

void Animator::animate_left(const EyeParams& params, uint32_t duration_ms, EaseType ease) {
    m_main_anim.start_left = m_current_left;
    m_main_anim.start_right = m_current_right;
    m_main_anim.target_left = params;
    m_main_anim.target_right = m_current_right;  // Keep current right
    m_main_anim.start_time = millis();
    m_main_anim.duration = duration_ms;
    m_main_anim.easing = ease;
    m_main_anim.active = true;
}

void Animator::animate_right(const EyeParams& params, uint32_t duration_ms, EaseType ease) {
    m_main_anim.start_left = m_current_left;
    m_main_anim.start_right = m_current_right;
    m_main_anim.target_left = m_current_left;  // Keep current left
    m_main_anim.target_right = params;
    m_main_anim.start_time = millis();
    m_main_anim.duration = duration_ms;
    m_main_anim.easing = ease;
    m_main_anim.active = true;
}

bool Animator::update() {
    if (!m_main_anim.active) {
        return false;
    }

    uint32_t elapsed = millis() - m_main_anim.start_time;

    if (elapsed >= m_main_anim.duration) {
        // Animation complete
        m_current_left = m_main_anim.target_left;
        m_current_right = m_main_anim.target_right;
        m_main_anim.active = false;
        return false;
    }

    // Calculate progress with easing
    float raw_progress = (float)elapsed / (float)m_main_anim.duration;
    float progress = apply_easing(raw_progress, m_main_anim.easing);

    // Interpolate parameters
    m_current_left = EyeParams::lerp(m_main_anim.start_left, m_main_anim.target_left, progress);
    m_current_right = EyeParams::lerp(m_main_anim.start_right, m_main_anim.target_right, progress);

    return true;
}

void Animator::stop() {
    m_main_anim.active = false;
}
