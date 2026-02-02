/**
 * Look Controller Implementation
 */

#include "look_controller.h"
#include <cmath>

LookController::LookController()
    : m_current_x(0.0f)
    , m_current_y(0.0f)
    , m_animating(false)
    , m_start_x(0.0f)
    , m_start_y(0.0f)
    , m_target_x(0.0f)
    , m_target_y(0.0f)
    , m_anim_start(0)
    , m_anim_duration(200)
    , m_random_enabled(true)
    , m_random_min_interval(1500)
    , m_random_max_interval(4000)
    , m_next_look_time(0)
    , m_random_max_x(0.5f)
    , m_random_max_y(0.3f) {
    schedule_next_look();
}

float LookController::random_float() {
    return (float)random(1000) / 1000.0f;
}

float LookController::ease_out(float t) {
    return 1.0f - (1.0f - t) * (1.0f - t);
}

void LookController::schedule_next_look() {
    uint32_t interval = random(m_random_min_interval, m_random_max_interval);
    m_next_look_time = millis() + interval;
}

bool LookController::update(float& gaze_x, float& gaze_y) {
    uint32_t now = millis();

    // Check for random look trigger
    if (m_random_enabled && !m_animating && now >= m_next_look_time) {
        // Generate random target position
        float target_x = (random_float() * 2.0f - 1.0f) * m_random_max_x;
        float target_y = (random_float() * 2.0f - 1.0f) * m_random_max_y;

        // Occasionally look center
        if (random_float() < 0.3f) {
            target_x = 0.0f;
            target_y = 0.0f;
        }

        // Calculate distance for duration
        float dx = target_x - m_current_x;
        float dy = target_y - m_current_y;
        float distance = sqrtf(dx * dx + dy * dy);

        // Duration based on distance (100-300ms)
        uint32_t duration = (uint32_t)(100 + distance * 200);

        look_at(target_x, target_y, duration);
    }

    // Process animation
    if (m_animating) {
        uint32_t elapsed = now - m_anim_start;
        float progress = (float)elapsed / (float)m_anim_duration;

        if (progress >= 1.0f) {
            m_current_x = m_target_x;
            m_current_y = m_target_y;
            m_animating = false;
            schedule_next_look();
        } else {
            float eased = ease_out(progress);
            m_current_x = m_start_x + (m_target_x - m_start_x) * eased;
            m_current_y = m_start_y + (m_target_y - m_start_y) * eased;
        }
    }

    gaze_x = m_current_x;
    gaze_y = m_current_y;

    return m_animating;
}

void LookController::look_at(float x, float y, uint32_t duration_ms) {
    m_start_x = m_current_x;
    m_start_y = m_current_y;
    m_target_x = constrain(x, -1.0f, 1.0f);
    m_target_y = constrain(y, -1.0f, 1.0f);
    m_anim_start = millis();
    m_anim_duration = duration_ms;
    m_animating = true;
}

void LookController::look_center(uint32_t duration_ms) {
    look_at(0.0f, 0.0f, duration_ms);
}

void LookController::set_random_look(bool enabled) {
    m_random_enabled = enabled;
    if (enabled) {
        schedule_next_look();
    }
}

void LookController::set_random_interval(uint32_t min_ms, uint32_t max_ms) {
    m_random_min_interval = min_ms;
    m_random_max_interval = max_ms;
}

void LookController::set_random_range(float max_x, float max_y) {
    m_random_max_x = constrain(max_x, 0.0f, 1.0f);
    m_random_max_y = constrain(max_y, 0.0f, 1.0f);
}
