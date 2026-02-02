/**
 * Blink Controller - Natural eye blink behavior
 */

#ifndef BLINK_CONTROLLER_H
#define BLINK_CONTROLLER_H

#include <Arduino.h>
#include "../eyes/eye_params.h"

/**
 * Blink states
 */
enum class BlinkState {
    IDLE,
    CLOSING,
    CLOSED,
    OPENING
};

/**
 * Blink Controller - manages automatic and manual blinking
 */
class BlinkController {
public:
    BlinkController();

    /**
     * Update blink state (call every frame)
     * @param left Output modified left eye params
     * @param right Output modified right eye params
     * @return true if blink is active and params were modified
     */
    bool update(EyeParams& left, EyeParams& right);

    /**
     * Trigger a manual blink
     */
    void blink();

    /**
     * Trigger a wink (single eye)
     * @param left_eye true for left eye, false for right
     */
    void wink(bool left_eye);

    /**
     * Set blink interval range (random between min and max)
     */
    void set_interval(uint32_t min_ms, uint32_t max_ms);

    /**
     * Set blink animation speed
     */
    void set_speed(uint32_t close_ms, uint32_t open_ms);

    /**
     * Enable/disable automatic blinking
     */
    void set_auto_blink(bool enabled);

    /**
     * Check if currently blinking
     */
    bool is_blinking() const { return m_state != BlinkState::IDLE; }

    /**
     * Set double blink probability (0.0 - 1.0)
     */
    void set_double_blink_chance(float chance);

private:
    void start_blink(bool left, bool right);
    void schedule_next_blink();
    float random_float();

    BlinkState m_state;

    // Which eyes are blinking
    bool m_blink_left;
    bool m_blink_right;

    // Timing
    uint32_t m_state_start_time;
    uint32_t m_close_duration;
    uint32_t m_open_duration;
    uint32_t m_closed_hold;

    // Auto-blink settings
    bool m_auto_enabled;
    uint32_t m_min_interval;
    uint32_t m_max_interval;
    uint32_t m_next_blink_time;

    // Double blink
    float m_double_blink_chance;
    bool m_pending_double_blink;

    // Current lid positions (0 = open, 1 = closed)
    float m_left_lid;
    float m_right_lid;
};

#endif // BLINK_CONTROLLER_H
