/**
 * IMU Handler Implementation - QMI8658
 * Provides tilt tracking, pickup detection, and shake detection
 */

#include "imu_handler.h"
#include <cmath>

ImuHandler::ImuHandler()
    : initialized(false)
    , accelX(0.0f)
    , accelY(0.0f)
    , accelZ(1.0f)
    , smoothAccelX(0.0f)
    , smoothAccelY(0.0f)
    , smoothAccelZ(1.0f)
    , tiltX(0.0f)
    , tiltY(0.0f)
    , tiltGazeX(0.0f)
    , tiltGazeY(0.0f)
    , beingHeld(false)
    , flipped(false)
    , wasFlat(true)
    , accelMagnitude(1.0f)
    , prevMagnitude(1.0f)
    , lastFlatTime(0)
    , shakeCount(0)
    , lastShakeTime(0)
    , lastShakeMagnitude(0.0f)
    , tiltGazeEnabled(true)
    , orientation(Orientation::Normal)
    , tiltDuration(0.0f)
    , tiltStartTime(0) {
}

void ImuHandler::writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(QMI8658_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

uint8_t ImuHandler::readRegister(uint8_t reg) {
    Wire.beginTransmission(QMI8658_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)QMI8658_ADDR, (uint8_t)1);
    return Wire.read();
}

bool ImuHandler::begin() {
    // Wire should already be initialized by touch handler
    // Check WHO_AM_I register
    uint8_t who_am_i = readRegister(QMI8658_WHO_AM_I);

    if (who_am_i != 0x05) {  // Expected QMI8658 ID
        Serial.printf("QMI8658 not found (ID: 0x%02X), continuing without IMU\n", who_am_i);
        return false;
    }

    Serial.printf("QMI8658 found (ID: 0x%02X)\n", who_am_i);

    // Soft reset
    writeRegister(QMI8658_CTRL1, 0x40);
    delay(10);

    // Configure accelerometer: ±4g range, 500Hz ODR
    writeRegister(QMI8658_CTRL2, 0x15);

    // Configure gyroscope: ±512 dps range, 500Hz ODR
    writeRegister(QMI8658_CTRL3, 0x45);

    // Enable accelerometer and gyroscope
    writeRegister(QMI8658_CTRL7, 0x03);

    delay(10);

    initialized = true;
    lastFlatTime = millis();
    Serial.println("IMU handler initialized");

    return true;
}

bool ImuHandler::readSensors() {
    if (!initialized) return false;

    // Read accelerometer data (6 bytes)
    Wire.beginTransmission(QMI8658_ADDR);
    Wire.write(QMI8658_ACCEL_DATA);
    Wire.endTransmission(false);

    Wire.requestFrom((uint8_t)QMI8658_ADDR, (uint8_t)6);

    if (Wire.available() < 6) return false;

    uint8_t data[6];
    for (int i = 0; i < 6; i++) {
        data[i] = Wire.read();
    }

    // Parse accelerometer (little-endian)
    int16_t raw_ax = (int16_t)((data[1] << 8) | data[0]);
    int16_t raw_ay = (int16_t)((data[3] << 8) | data[2]);
    int16_t raw_az = (int16_t)((data[5] << 8) | data[4]);

    // Convert to g (±4g range, 16-bit)
    const float accel_scale = 4.0f / 32768.0f;
    accelX = raw_ax * accel_scale;
    accelY = raw_ay * accel_scale;
    accelZ = raw_az * accel_scale;

    return true;
}

ImuEvent ImuHandler::detectGesture(float dt) {
    uint32_t now = millis();

    // Calculate acceleration magnitude
    prevMagnitude = accelMagnitude;
    accelMagnitude = sqrtf(accelX * accelX + accelY * accelY + accelZ * accelZ);

    // Smooth accelerometer values for tilt calculation
    const float smoothFactor = 0.1f;
    smoothAccelX += (accelX - smoothAccelX) * smoothFactor;
    smoothAccelY += (accelY - smoothAccelY) * smoothFactor;
    smoothAccelZ += (accelZ - smoothAccelZ) * smoothFactor;

    // Calculate tilt angles from smoothed accelerometer
    tiltX = atan2f(smoothAccelX, sqrtf(smoothAccelY * smoothAccelY + smoothAccelZ * smoothAccelZ)) * 180.0f / M_PI;
    tiltY = atan2f(smoothAccelY, sqrtf(smoothAccelX * smoothAccelX + smoothAccelZ * smoothAccelZ)) * 180.0f / M_PI;

    // Calculate gaze offset from tilt
    if (tiltGazeEnabled) {
        tiltGazeX = constrain(tiltX / TILT_MAX_ANGLE, -1.0f, 1.0f);
        tiltGazeY = constrain(-tiltY / TILT_MAX_ANGLE, -1.0f, 1.0f);  // Invert Y
    } else {
        tiltGazeX = 0.0f;
        tiltGazeY = 0.0f;
    }

    // Check if device is flipped (Z negative)
    flipped = (smoothAccelZ < -0.5f);

    // Orientation detection
    if (smoothAccelZ < FACE_DOWN_THRESHOLD) {
        // Face-down: screen toward floor
        orientation = Orientation::FaceDown;
        tiltDuration = 0.0f;
        tiltStartTime = 0;
    } else {
        // Check for sustained tilt (>45° from flat)
        float totalTilt = sqrtf(tiltX * tiltX + tiltY * tiltY);
        if (totalTilt > 45.0f) {
            if (tiltStartTime == 0) {
                tiltStartTime = now;
            }
            tiltDuration = (now - tiltStartTime) / 1000.0f;
            if (tiltDuration >= TILT_LONG_SECONDS) {
                orientation = Orientation::TiltedLong;
            } else {
                orientation = Orientation::Normal;
            }
        } else {
            orientation = Orientation::Normal;
            tiltDuration = 0.0f;
            tiltStartTime = 0;
        }
    }

    // Check if device is being held (not flat on table)
    // When flat, Z should be close to 1g, X and Y close to 0
    float deviationFromFlat = fabsf(accelMagnitude - 1.0f) +
                              fabsf(smoothAccelX) + fabsf(smoothAccelY);
    bool isFlat = (deviationFromFlat < HELD_THRESHOLD) && (smoothAccelZ > 0.8f);

    if (isFlat) {
        lastFlatTime = now;
        wasFlat = true;
    }

    beingHeld = !isFlat && (now - lastFlatTime > 200);

    // Pickup detection: was flat, now being held with acceleration spike
    if (wasFlat && beingHeld) {
        float magChange = fabsf(accelMagnitude - prevMagnitude);
        if (magChange > PICKUP_THRESHOLD) {
            wasFlat = false;
            Serial.println("Pickup detected!");
            return ImuEvent::PickedUp;
        }
    }

    // Knock detection: single very high acceleration spike
    // Must be higher than shake threshold and not part of a shake sequence
    if (accelMagnitude > KNOCK_THRESHOLD) {
        // Only trigger knock if this is a fresh spike (not part of ongoing shake)
        if (now - lastShakeTime > 300) {
            lastShakeTime = now;
            shakeCount = 1;
            Serial.println("Knock detected!");
            return ImuEvent::Knocked;
        }
    }

    // Shake detection: multiple high acceleration spikes
    if (accelMagnitude > SHAKE_THRESHOLD) {
        if (now - lastShakeTime < 500) {
            shakeCount++;
            if (shakeCount >= 3) {
                shakeCount = 0;
                Serial.println("Shake detected!");
                return ImuEvent::ShookHard;
            }
        } else {
            shakeCount = 1;
        }
        lastShakeTime = now;
    }

    return ImuEvent::None;
}

ImuEvent ImuHandler::update(float dt) {
    if (!initialized) {
        tiltGazeX = 0.0f;
        tiltGazeY = 0.0f;
        return ImuEvent::None;
    }

    if (!readSensors()) {
        return ImuEvent::None;
    }

    return detectGesture(dt);
}
