/**
 * IMU Handler - QMI8658 6-axis accelerometer/gyroscope
 * Used for tilt tracking, pickup detection, and shake detection
 */

#ifndef IMU_HANDLER_H
#define IMU_HANDLER_H

#include <Arduino.h>
#include <Wire.h>

// QMI8658 I2C address
#define QMI8658_ADDR 0x6B

// QMI8658 registers
#define QMI8658_WHO_AM_I     0x00
#define QMI8658_CTRL1        0x02
#define QMI8658_CTRL2        0x03
#define QMI8658_CTRL3        0x04
#define QMI8658_CTRL7        0x08
#define QMI8658_ACCEL_DATA   0x35
#define QMI8658_GYRO_DATA    0x3B

// IMU events that can be detected
enum class ImuEvent {
    None,
    PickedUp,     // Device was lifted
    ShookHard,    // Device was shaken (3+ spikes within 500ms)
    Knocked,      // Device was knocked (single hard impact)
    TiltChanged   // Significant tilt change
};

// Device orientation states
enum class Orientation {
    Normal,       // Upright or slightly tilted
    FaceDown,     // Screen facing floor (hiding)
    TiltedLong    // Tilted >45° for extended time (uncomfortable)
};

/**
 * IMU Handler - manages accelerometer/gyroscope input
 */
class ImuHandler {
public:
    ImuHandler();

    /**
     * Initialize IMU sensor
     * @return true if successful
     */
    bool begin();

    /**
     * Update IMU state (call every frame)
     * @param dt Delta time in seconds
     * @return ImuEvent if gesture detected, None otherwise
     */
    ImuEvent update(float dt);

    /**
     * Get current accelerometer values (g)
     */
    float getAccelX() const { return accelX; }
    float getAccelY() const { return accelY; }
    float getAccelZ() const { return accelZ; }

    /**
     * Get calculated tilt angles (degrees from flat)
     */
    float getTiltX() const { return tiltX; }
    float getTiltY() const { return tiltY; }

    /**
     * Get gaze offset based on tilt (-1 to 1)
     * Eyes follow the direction of tilt
     */
    float getTiltGazeX() const { return tiltGazeX; }
    float getTiltGazeY() const { return tiltGazeY; }

    /**
     * Check if device is being held (not flat on table)
     */
    bool isBeingHeld() const { return beingHeld; }

    /**
     * Check if device is upside down
     */
    bool isFlipped() const { return flipped; }

    /**
     * Get current device orientation
     */
    Orientation getOrientation() const { return orientation; }

    /**
     * Check if device is face-down (screen toward floor)
     */
    bool isFaceDown() const { return orientation == Orientation::FaceDown; }

    /**
     * Get how long device has been tilted >45° (seconds)
     */
    float getTiltDuration() const { return tiltDuration; }

    /**
     * Enable/disable tilt-based gaze tracking
     */
    void setTiltGazeEnabled(bool enabled) { tiltGazeEnabled = enabled; }
    bool isTiltGazeEnabled() const { return tiltGazeEnabled; }

private:
    bool readSensors();
    void writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);
    ImuEvent detectGesture(float dt);

    bool initialized;

    // Raw accelerometer data (g)
    float accelX, accelY, accelZ;

    // Smoothed accelerometer data
    float smoothAccelX, smoothAccelY, smoothAccelZ;

    // Calculated tilt (degrees)
    float tiltX, tiltY;

    // Gaze offset from tilt
    float tiltGazeX, tiltGazeY;

    // State
    bool beingHeld;
    bool flipped;
    bool wasFlat;

    // Pickup detection
    float accelMagnitude;
    float prevMagnitude;
    uint32_t lastFlatTime;

    // Shake detection
    int shakeCount;
    uint32_t lastShakeTime;
    float lastShakeMagnitude;

    // Settings
    bool tiltGazeEnabled;

    // Orientation detection
    Orientation orientation;
    float tiltDuration;           // How long tilted >45°
    uint32_t tiltStartTime;       // When tilt started

    // Thresholds
    static constexpr float HELD_THRESHOLD = 0.3f;      // g deviation from 1.0
    static constexpr float PICKUP_THRESHOLD = 0.5f;    // g spike
    static constexpr float SHAKE_THRESHOLD = 2.5f;     // g (requires 3+ spikes)
    static constexpr float KNOCK_THRESHOLD = 4.0f;     // g (single hard impact)
    static constexpr float TILT_MAX_ANGLE = 45.0f;     // degrees for full gaze
    static constexpr float FACE_DOWN_THRESHOLD = -0.7f;  // g (Z axis for face-down)
    static constexpr float TILT_LONG_SECONDS = 5.0f;   // seconds tilted for uncomfortable
};

#endif // IMU_HANDLER_H
