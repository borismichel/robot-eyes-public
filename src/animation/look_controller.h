/**
 * Look Controller - Eye gaze direction management
 */

#ifndef LOOK_CONTROLLER_H
#define LOOK_CONTROLLER_H

#include <Arduino.h>

/**
 * Look Controller - manages where the eyes are looking
 */
class LookController {
public:
    LookController();

    /**
     * Update look animation (call every frame)
     * @param gaze_x Output current X gaze (-1 to 1)
     * @param gaze_y Output current Y gaze (-1 to 1)
     * @return true if gaze is animating
     */
    bool update(float& gaze_x, float& gaze_y);

    /**
     * Look at a specific position
     * @param x Target X (-1 to 1, left to right)
     * @param y Target Y (-1 to 1, up to down)
     * @param duration_ms Animation duration
     */
    void look_at(float x, float y, uint32_t duration_ms = 200);

    /**
     * Look straight ahead
     */
    void look_center(uint32_t duration_ms = 200);

    /**
     * Enable random looking behavior
     */
    void set_random_look(bool enabled);

    /**
     * Set random look interval
     */
    void set_random_interval(uint32_t min_ms, uint32_t max_ms);

    /**
     * Set maximum random gaze offset
     */
    void set_random_range(float max_x, float max_y);

    /**
     * Get current gaze position
     */
    float get_x() const { return m_current_x; }
    float get_y() const { return m_current_y; }

    /**
     * Check if currently animating
     */
    bool is_animating() const { return m_animating; }

private:
    void schedule_next_look();
    float random_float();
    float ease_out(float t);

    // Current gaze position
    float m_current_x;
    float m_current_y;

    // Animation state
    bool m_animating;
    float m_start_x;
    float m_start_y;
    float m_target_x;
    float m_target_y;
    uint32_t m_anim_start;
    uint32_t m_anim_duration;

    // Random look settings
    bool m_random_enabled;
    uint32_t m_random_min_interval;
    uint32_t m_random_max_interval;
    uint32_t m_next_look_time;
    float m_random_max_x;
    float m_random_max_y;
};

#endif // LOOK_CONTROLLER_H
