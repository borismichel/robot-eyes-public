/**
 * @file text_renderer.h
 * @brief Shared 5x7 bitmap font and drawing utilities
 *
 * Provides text rendering with 90° CCW rotation for the display.
 * All drawing functions map screen coordinates to buffer coordinates:
 *   bx = screenY;  by = bufH - 1 - screenX
 *
 * Supports: 0-9, A-Z (a-z maps to uppercase), space, : . - / ' ? !
 */

#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

#include <Arduino.h>

namespace TextRenderer {

/**
 * @brief Fill a rectangle (screen coords, rotated to buffer)
 */
void drawFilledRect(uint16_t* buf, int16_t bufW, int16_t bufH,
                    int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

/**
 * @brief Draw a single character at screen position
 * @param scale Pixel scale factor (default 3)
 */
void drawChar(uint16_t* buf, int16_t bufW, int16_t bufH,
              int16_t x, int16_t y, char c, uint16_t color, int scale = 3);

/**
 * @brief Draw a string starting at screen position
 * @param scale Pixel scale factor (default 3)
 */
void drawText(uint16_t* buf, int16_t bufW, int16_t bufH,
              int16_t x, int16_t y, const char* text, uint16_t color, int scale = 3);

/**
 * @brief Draw a string centered horizontally at centerX
 * @param scale Pixel scale factor (default 3)
 */
void drawCenteredText(uint16_t* buf, int16_t bufW, int16_t bufH,
                      int16_t centerX, int16_t y, const char* text,
                      uint16_t color, int scale = 3);

/**
 * @brief Draw text with word wrapping
 * @param scale Pixel scale factor
 * @param maxCharsPerLine Maximum characters per line before wrapping
 */
void drawWrappedText(uint16_t* buf, int16_t bufW, int16_t bufH,
                     int16_t centerX, int16_t startY, const char* text,
                     uint16_t color, int scale, int maxCharsPerLine);

/**
 * @brief Draw a single digit (0-9) at large scale
 * @param scale Pixel scale factor (default 5)
 */
void drawLargeDigit(uint16_t* buf, int16_t bufW, int16_t bufH,
                    int16_t x, int16_t y, int digit, uint16_t color, int scale = 5);

/**
 * @brief Clear buffer to a solid color
 */
void clearBuffer(uint16_t* buf, int16_t bufW, int16_t bufH, uint16_t color);

} // namespace TextRenderer

#endif // TEXT_RENDERER_H
