/**
 * Touch Handler - FT3168 capacitive touch input
 */

#ifndef TOUCH_HANDLER_H
#define TOUCH_HANDLER_H

#include <Arduino.h>
#include <Wire.h>
#include "../behavior/emotion_types.h"

// FT3168 I2C address
#define FT3168_ADDR 0x38

// Touch pins
#define TOUCH_SDA 39
#define TOUCH_SCL 40
#define TOUCH_INT 21

/**
 * Touch event callback type
 */
typedef void (*TouchCallback)(TriggerEvent event, int16_t x, int16_t y);

/**
 * Touch Handler - manages capacitive touch input
 */
class TouchHandler {
public:
    TouchHandler();

    /**
     * Initialize touch controller
     */
    bool init();

    /**
     * Update touch state (call every frame)
     * @return TriggerEvent if gesture detected, NONE otherwise
     */
    TriggerEvent update();

    /**
     * Get last touch position
     */
    int16_t get_x() const { return m_last_x; }
    int16_t get_y() const { return m_last_y; }

    /**
     * Check if currently touched
     */
    bool is_touched() const { return m_touched; }

    /**
     * Set callback for touch events
     */
    void set_callback(TouchCallback callback);

private:
    bool read_touch();
    TriggerEvent detect_gesture();

    // Touch state
    bool m_initialized;
    bool m_touched;
    bool m_was_touched;

    // Current/last position
    int16_t m_x;
    int16_t m_y;
    int16_t m_last_x;
    int16_t m_last_y;

    // Gesture detection
    int16_t m_start_x;
    int16_t m_start_y;
    uint32_t m_touch_start_time;
    uint32_t m_last_tap_time;
    int m_tap_count;

    // Thresholds
    static const int16_t SWIPE_THRESHOLD = 50;
    static const uint32_t LONG_PRESS_TIME = 800;
    static const uint32_t DOUBLE_TAP_TIME = 300;

    // Callback
    TouchCallback m_callback;
};

#endif // TOUCH_HANDLER_H
