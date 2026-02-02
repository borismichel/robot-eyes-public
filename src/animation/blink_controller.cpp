/**
 * Blink Controller Implementation
 */

#include "blink_controller.h"

BlinkController::BlinkController()
    : m_state(BlinkState::IDLE)
    , m_blink_left(false)
    , m_blink_right(false)
    , m_state_start_time(0)
    , m_close_duration(60)
    , m_open_duration(80)
    , m_closed_hold(30)
    , m_auto_enabled(true)
    , m_min_interval(2000)
    , m_max_interval(5000)
    , m_next_blink_time(0)
    , m_double_blink_chance(0.15f)
    , m_pending_double_blink(false)
    , m_left_lid(0.0f)
    , m_right_lid(0.0f) {
    schedule_next_blink();
}

float BlinkController::random_float() {
    return (float)random(1000) / 1000.0f;
}

void BlinkController::schedule_next_blink() {
    uint32_t interval = random(m_min_interval, m_max_interval);
    m_next_blink_time = millis() + interval;
}

void BlinkController::start_blink(bool left, bool right) {
    m_blink_left = left;
    m_blink_right = right;
    m_state = BlinkState::CLOSING;
    m_state_start_time = millis();

    // Decide on double blink
    m_pending_double_blink = (random_float() < m_double_blink_chance);
}

bool BlinkController::update(EyeParams& left, EyeParams& right) {
    uint32_t now = millis();

    // Check for auto-blink trigger
    if (m_auto_enabled && m_state == BlinkState::IDLE && now >= m_next_blink_time) {
        start_blink(true, true);
    }

    // Process blink animation
    if (m_state == BlinkState::IDLE) {
        return false;
    }

    uint32_t elapsed = now - m_state_start_time;

    switch (m_state) {
        case BlinkState::CLOSING: {
            float progress = (float)elapsed / (float)m_close_duration;
            if (progress >= 1.0f) {
                progress = 1.0f;
                m_state = BlinkState::CLOSED;
                m_state_start_time = now;
            }

            // Apply closing with ease-in
            float eased = progress * progress;
            if (m_blink_left) m_left_lid = eased;
            if (m_blink_right) m_right_lid = eased;
            break;
        }

        case BlinkState::CLOSED: {
            if (elapsed >= m_closed_hold) {
                m_state = BlinkState::OPENING;
                m_state_start_time = now;
            }

            if (m_blink_left) m_left_lid = 1.0f;
            if (m_blink_right) m_right_lid = 1.0f;
            break;
        }

        case BlinkState::OPENING: {
            float progress = (float)elapsed / (float)m_open_duration;
            if (progress >= 1.0f) {
                progress = 1.0f;

                // Check for double blink
                if (m_pending_double_blink) {
                    m_pending_double_blink = false;
                    m_state = BlinkState::CLOSING;
                    m_state_start_time = now + 50;  // Short pause between blinks
                } else {
                    m_state = BlinkState::IDLE;
                    schedule_next_blink();
                }
            }

            // Apply opening with ease-out
            float eased = 1.0f - (1.0f - progress) * (1.0f - progress);
            if (m_blink_left) m_left_lid = 1.0f - eased;
            if (m_blink_right) m_right_lid = 1.0f - eased;
            break;
        }

        default:
            break;
    }

    // Apply lid positions to eye params
    // Blink closes from top
    left.top_lid = max(left.top_lid, m_left_lid);
    right.top_lid = max(right.top_lid, m_right_lid);

    return true;
}

void BlinkController::blink() {
    if (m_state == BlinkState::IDLE) {
        start_blink(true, true);
    }
}

void BlinkController::wink(bool left_eye) {
    if (m_state == BlinkState::IDLE) {
        start_blink(left_eye, !left_eye);
    }
}

void BlinkController::set_interval(uint32_t min_ms, uint32_t max_ms) {
    m_min_interval = min_ms;
    m_max_interval = max_ms;
}

void BlinkController::set_speed(uint32_t close_ms, uint32_t open_ms) {
    m_close_duration = close_ms;
    m_open_duration = open_ms;
}

void BlinkController::set_auto_blink(bool enabled) {
    m_auto_enabled = enabled;
    if (enabled && m_state == BlinkState::IDLE) {
        schedule_next_blink();
    }
}

void BlinkController::set_double_blink_chance(float chance) {
    m_double_blink_chance = constrain(chance, 0.0f, 1.0f);
}
