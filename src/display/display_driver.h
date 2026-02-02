/**
 * Display Driver for Waveshare ESP32-S3-Touch-AMOLED-1.8
 * SH8601 AMOLED controller via QSPI using Arduino_GFX library
 */

#ifndef DISPLAY_DRIVER_H
#define DISPLAY_DRIVER_H

#include <Arduino.h>
#include <lvgl.h>
#include "pin_config.h"

// Display dimensions (landscape orientation after rotation)
#define DISPLAY_WIDTH  448
#define DISPLAY_HEIGHT 368

// Native display dimensions (portrait)
#define NATIVE_WIDTH   LCD_WIDTH   // 368
#define NATIVE_HEIGHT  LCD_HEIGHT  // 448

// Display buffer size (larger = smoother, but uses more RAM)
#define DISPLAY_BUF_SIZE (DISPLAY_WIDTH * 40)

/**
 * Initialize the display hardware and LVGL
 * @return true on success
 */
bool display_init();

/**
 * Set display brightness
 * @param brightness 0-255
 */
void display_set_brightness(uint8_t brightness);

/**
 * Get the LVGL display object
 */
lv_disp_t* display_get();

/**
 * Must be called in the main loop to handle LVGL tasks
 */
void display_update();

#endif // DISPLAY_DRIVER_H
