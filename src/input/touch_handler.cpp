/**
 * Touch Handler Implementation - FT3168
 */

#include "touch_handler.h"

TouchHandler::TouchHandler()
    : m_initialized(false)
    , m_touched(false)
    , m_was_touched(false)
    , m_x(0)
    , m_y(0)
    , m_last_x(0)
    , m_last_y(0)
    , m_start_x(0)
    , m_start_y(0)
    , m_touch_start_time(0)
    , m_last_tap_time(0)
    , m_tap_count(0)
    , m_callback(nullptr) {
}

bool TouchHandler::init() {
    Wire.begin(TOUCH_SDA, TOUCH_SCL);

    // Check if FT3168 is present
    Wire.beginTransmission(FT3168_ADDR);
    if (Wire.endTransmission() != 0) {
        Serial.println("ERROR: FT3168 touch controller not found!");
        return false;
    }

    // Read chip ID
    Wire.beginTransmission(FT3168_ADDR);
    Wire.write(0xA3);  // Chip ID register
    Wire.endTransmission(false);
    Wire.requestFrom(FT3168_ADDR, 1);

    if (Wire.available()) {
        uint8_t chip_id = Wire.read();
        Serial.printf("Touch controller ID: 0x%02X\n", chip_id);
    }

    // Configure interrupt pin
    pinMode(TOUCH_INT, INPUT);

    m_initialized = true;
    Serial.println("Touch handler initialized");

    return true;
}

bool TouchHandler::read_touch() {
    if (!m_initialized) return false;

    // Read touch data from FT3168
    Wire.beginTransmission(FT3168_ADDR);
    Wire.write(0x02);  // Touch data register
    Wire.endTransmission(false);

    uint8_t data[6];
    Wire.requestFrom(FT3168_ADDR, 6);

    if (Wire.available() < 6) {
        return m_touched;
    }

    for (int i = 0; i < 6; i++) {
        data[i] = Wire.read();
    }

    // Parse touch data
    // data[0]: Number of touch points
    // data[1-2]: X position (12-bit)
    // data[3-4]: Y position (12-bit)
    // data[5]: Touch event (0=down, 1=up, 2=contact)

    uint8_t touch_points = data[0] & 0x0F;

    if (touch_points > 0) {
        // Get coordinates (landscape mode - swap and adjust)
        int16_t raw_x = ((data[1] & 0x0F) << 8) | data[2];
        int16_t raw_y = ((data[3] & 0x0F) << 8) | data[4];

        // Rotate coordinates for landscape mode
        // Native: 368x448, Landscape: 448x368
        m_x = 448 - raw_y;  // Swap and invert
        m_y = raw_x;

        m_touched = true;
    } else {
        m_touched = false;
    }

    return m_touched;
}

TriggerEvent TouchHandler::update() {
    m_was_touched = m_touched;
    read_touch();

    TriggerEvent event = TriggerEvent::NONE;
    uint32_t now = millis();

    // Touch just started
    if (m_touched && !m_was_touched) {
        m_start_x = m_x;
        m_start_y = m_y;
        m_touch_start_time = now;
    }

    // Touch just ended - detect gesture
    if (!m_touched && m_was_touched) {
        m_last_x = m_start_x;
        m_last_y = m_start_y;
        event = detect_gesture();
    }

    // Currently touching - check for long press
    if (m_touched) {
        uint32_t duration = now - m_touch_start_time;
        if (duration >= LONG_PRESS_TIME) {
            // Only trigger once
            if (m_touch_start_time > 0) {
                m_touch_start_time = 0;  // Prevent re-trigger
                event = TriggerEvent::LONG_PRESS;
            }
        }
    }

    // Fire callback
    if (event != TriggerEvent::NONE && m_callback) {
        m_callback(event, m_last_x, m_last_y);
    }

    return event;
}

TriggerEvent TouchHandler::detect_gesture() {
    uint32_t now = millis();
    uint32_t duration = now - m_touch_start_time;

    // Calculate movement
    int16_t dx = m_x - m_start_x;
    int16_t dy = m_y - m_start_y;
    int16_t abs_dx = abs(dx);
    int16_t abs_dy = abs(dy);

    // Check for swipe
    if (abs_dx > SWIPE_THRESHOLD || abs_dy > SWIPE_THRESHOLD) {
        if (abs_dx > abs_dy) {
            // Horizontal swipe
            return (dx > 0) ? TriggerEvent::SWIPE_RIGHT : TriggerEvent::SWIPE_LEFT;
        } else {
            // Vertical swipe
            return (dy > 0) ? TriggerEvent::SWIPE_DOWN : TriggerEvent::SWIPE_UP;
        }
    }

    // Check for tap (short touch with minimal movement)
    if (duration < LONG_PRESS_TIME && abs_dx < 20 && abs_dy < 20) {
        // Check for double tap
        if (now - m_last_tap_time < DOUBLE_TAP_TIME) {
            m_tap_count++;
            if (m_tap_count >= 2) {
                m_tap_count = 0;
                m_last_tap_time = 0;
                return TriggerEvent::DOUBLE_TAP;
            }
        } else {
            m_tap_count = 1;
        }

        m_last_tap_time = now;
        return TriggerEvent::TAP;
    }

    return TriggerEvent::NONE;
}

void TouchHandler::set_callback(TouchCallback callback) {
    m_callback = callback;
}
