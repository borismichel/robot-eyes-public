/**
 * @file eye_renderer.h
 * @brief Software renderer for parametric robot eyes
 *
 * This module renders EyeShape parameters to RGB565 framebuffers. It supports:
 *   - Rounded rectangle base shapes with configurable corner radius
 *   - Independent eyelid animation (top and bottom)
 *   - Corner Y offsets for expressive deformation (happy, sad, angry)
 *   - Pinch effects for pointed shapes (yawn "> <")
 *   - Curve effects for crescent/half-moon shapes (content)
 *
 * RENDERING APPROACH:
 * Uses per-pixel scanline rendering rather than graphics primitives. This allows
 * complex shape modifications (pinch, curve, corner offsets) to be applied
 * mathematically at each pixel, enabling smooth transitions between any shapes.
 *
 * COORDINATE SYSTEM:
 * The physical display is rotated 90° clockwise from the buffer orientation:
 *   - Buffer X axis → Screen vertical (top to bottom)
 *   - Buffer Y axis → Screen horizontal (left to right)
 *
 * This affects how shape parameters map to visual appearance:
 *   - "Width" in buffer = vertical extent on screen
 *   - "Top lid" fills from buffer LEFT = screen TOP
 *   - Eye "height" in buffer = horizontal extent on screen
 *
 * BUFFER MODES:
 * Two buffer configurations are supported:
 *   1. Single eye buffer (EYE_BUF_*) - For rendering one eye at a time
 *   2. Combined buffer (COMBINED_BUF_*) - For rendering both eyes together
 *
 * @author Robot Eyes Project
 * @date 2025
 */

#ifndef EYE_RENDERER_H
#define EYE_RENDERER_H

#include <Arduino.h>
#include "eye_shape.h"

//-----------------------------------------------------------------------------
// Buffer Dimensions
//-----------------------------------------------------------------------------

/**
 * Single eye buffer width in pixels.
 * Sized to accommodate eye base width (120px) plus maximum gaze offset (±50px).
 * This dimension corresponds to screen vertical axis after 90° rotation.
 */
#define EYE_BUF_WIDTH   220

/**
 * Single eye buffer height in pixels.
 * Sized to accommodate eye base height (100px) plus maximum gaze offset (±60px).
 * This dimension corresponds to screen horizontal axis after 90° rotation.
 */
#define EYE_BUF_HEIGHT  220

/**
 * Combined buffer width for dual-eye rendering.
 * Horizontal extent containing both eyes side-by-side in buffer space.
 * Sized to fit within 16px progress bar margins: 368 - 32 = 336
 */
#define COMBINED_BUF_WIDTH  336

/**
 * Combined buffer height for dual-eye rendering.
 * Vertical extent in buffer space to stack both eyes with proper spacing.
 * Sized to fit within 16px progress bar margins: 448 - 32 = 416
 */
#define COMBINED_BUF_HEIGHT 416

//-----------------------------------------------------------------------------
// Color Definitions (RGB565 format)
//-----------------------------------------------------------------------------

/** Default eye fill color - Cyan (#00FFFF in RGB565) */
#define DEFAULT_EYE_COLOR  0x07FF

/** Background color - Black */
#define BG_COLOR        0x0000

/** Color preset count */
#define NUM_COLOR_PRESETS 8

/** Color preset table (RGB565) */
static const uint16_t COLOR_PRESETS[NUM_COLOR_PRESETS] = {
    0x07FF,  // Cyan
    0xF81F,  // Pink/Magenta
    0x07E0,  // Green
    0xFD20,  // Orange
    0x781F,  // Purple
    0xFFFF,  // White
    0xF800,  // Red
    0x041F,  // Blue
};

/** Color preset names */
static const char* COLOR_PRESET_NAMES[NUM_COLOR_PRESETS] = {
    "CYAN", "PINK", "GREEN", "ORANGE", "PURPLE", "WHITE", "RED", "BLUE"
};

//-----------------------------------------------------------------------------
// EyeRenderer Class
//-----------------------------------------------------------------------------

/**
 * @class EyeRenderer
 * @brief Renders parametric eye shapes to RGB565 framebuffers
 *
 * This class handles all low-level rendering of eye shapes. It converts the
 * high-level EyeShape parameters into pixel data that can be displayed.
 *
 * Usage example:
 * @code
 *   EyeRenderer renderer;
 *   uint16_t buffer[EYE_BUF_WIDTH * EYE_BUF_HEIGHT];
 *
 *   EyeShape happy;
 *   happy.outerCornerY = 0.2f;  // Raised outer corners
 *
 *   renderer.render(happy, buffer, EYE_BUF_WIDTH/2, EYE_BUF_HEIGHT/2, true);
 * @endcode
 */
class EyeRenderer {
public:
    /**
     * @brief Constructor - initializes renderer with default buffer dimensions
     */
    EyeRenderer();

    /**
     * @brief Set the eye fill color
     * @param color RGB565 color value
     */
    void setColor(uint16_t color) { eyeColor = color; }

    /**
     * @brief Get the current eye fill color
     */
    uint16_t getColor() const { return eyeColor; }

    /**
     * @brief Render an eye to buffer using default single-eye dimensions
     *
     * Legacy interface that uses EYE_BUF_WIDTH x EYE_BUF_HEIGHT buffer size.
     * For more control, use renderToBuf() instead.
     *
     * @param shape Eye shape parameters to render
     * @param buffer Pointer to RGB565 pixel buffer (must be EYE_BUF_* sized)
     * @param centerX X coordinate of eye center in buffer
     * @param centerY Y coordinate of eye center in buffer
     * @param isLeftEye True for left eye (affects asymmetric expressions)
     */
    void render(const EyeShape& shape, uint16_t* buffer,
                int16_t centerX, int16_t centerY, bool isLeftEye);

    /**
     * @brief Render an eye to a buffer with custom dimensions
     *
     * Primary rendering function supporting arbitrary buffer sizes. Use this
     * for combined dual-eye buffers or custom configurations.
     *
     * @param shape Eye shape parameters to render
     * @param buffer Pointer to RGB565 pixel buffer
     * @param bufWidth Buffer width (used for pixel indexing stride)
     * @param bufHeight Buffer height (used for bounds checking)
     * @param centerX X coordinate of eye center within buffer
     * @param centerY Y coordinate of eye center within buffer
     * @param isLeftEye True for left eye (affects corner orientation for
     *                  asymmetric expressions like Suspicious, Confused)
     * @param clearFirst If true, fills buffer with BG_COLOR before rendering.
     *                   Set false when rendering multiple eyes to same buffer.
     */
    void renderToBuf(const EyeShape& shape, uint16_t* buffer,
                     int16_t bufWidth, int16_t bufHeight,
                     int16_t centerX, int16_t centerY,
                     bool isLeftEye, bool clearFirst = true);

    /**
     * @brief Clear buffer to background color using default dimensions
     * @param buffer Pointer to buffer (must be EYE_BUF_* sized)
     */
    void clearBuffer(uint16_t* buffer);

    /**
     * @brief Clear buffer to background color with custom dimensions
     * @param buffer Pointer to pixel buffer
     * @param bufWidth Buffer width
     * @param bufHeight Buffer height
     */
    void clearBuffer(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight);

private:
    /** Current buffer width - set before each render operation */
    int16_t curBufWidth;

    /** Current buffer height - set before each render operation */
    int16_t curBufHeight;

    /** Current eye fill color (RGB565) */
    uint16_t eyeColor;

    /**
     * @brief Draw the main eye shape with all modifiers applied
     *
     * Renders a rounded rectangle with corner offsets, pinch, and curve effects.
     * Uses per-pixel evaluation to support complex shape morphing.
     *
     * @param buffer Target pixel buffer
     * @param x Left edge of bounding box in buffer
     * @param y Top edge of bounding box in buffer
     * @param w Width of eye shape
     * @param h Height of eye shape
     * @param r Corner radius
     * @param innerCornerY Vertical offset for inner corners (-1 to 1)
     * @param outerCornerY Vertical offset for outer corners (-1 to 1)
     * @param topPinch Top edge pinch factor (0 = flat, 1 = pointed)
     * @param bottomPinch Bottom edge pinch factor (0 = flat, 1 = pointed)
     * @param topCurve Top edge inward curve (0 = flat, 1 = curved)
     * @param bottomCurve Bottom edge inward curve (0 = flat, 1 = curved)
     * @param isLeftEye Affects corner orientation for asymmetric shapes
     */
    void drawRoundedRect(uint16_t* buffer, int16_t x, int16_t y,
                         int16_t w, int16_t h, int16_t r,
                         float innerCornerY, float outerCornerY,
                         float topPinch, float bottomPinch,
                         float topCurve, float bottomCurve,
                         bool isLeftEye);

    /**
     * @brief Apply top eyelid mask to rendered eye
     *
     * Fills pixels from the left edge (buffer X = screen top) with background
     * color to simulate eyelid closure. Only affects pixels that are already
     * EYE_COLOR to preserve rounded corners.
     *
     * @param buffer Target pixel buffer
     * @param eyeLeft Left edge of eye in buffer
     * @param eyeWidth Width of eye for calculating lid pixels
     * @param lidAmount Closure amount (0 = open, 1 = closed)
     * @param eyeY Top of eye bounding box
     * @param eyeHeight Height of eye for vertical bounds
     */
    void applyTopLid(uint16_t* buffer, int16_t eyeLeft, int16_t eyeWidth,
                     float lidAmount, int16_t eyeY, int16_t eyeHeight);

    /**
     * @brief Apply bottom eyelid mask to rendered eye
     *
     * Fills pixels from the right edge (buffer X = screen bottom) with
     * background color. Only affects EYE_COLOR pixels.
     *
     * @param buffer Target pixel buffer
     * @param eyeRight Right edge of eye in buffer
     * @param eyeWidth Width of eye for calculating lid pixels
     * @param lidAmount Closure amount (0 = open, 1 = closed)
     * @param eyeY Top of eye bounding box
     * @param eyeHeight Height of eye for vertical bounds
     */
    void applyBottomLid(uint16_t* buffer, int16_t eyeRight, int16_t eyeWidth,
                        float lidAmount, int16_t eyeY, int16_t eyeHeight);

    /**
     * @brief Draw a filled circle for subtractive effects
     *
     * Used to create crescent/half-moon shapes by subtracting a large circle
     * from the eye shape. The circle is filled with the specified color.
     *
     * @param buffer Target pixel buffer
     * @param cx Circle center X
     * @param cy Circle center Y
     * @param radius Circle radius
     * @param color Fill color (typically BG_COLOR for subtraction)
     */
    void drawFilledCircle(uint16_t* buffer, int16_t cx, int16_t cy,
                          int16_t radius, uint16_t color);

    /**
     * @brief Draw a star shape
     *
     * Renders a star with variable number of points. Used for dizzy/knocked
     * expressions. The star can rotate based on animPhase.
     *
     * @param buffer Target pixel buffer
     * @param cx Center X
     * @param cy Center Y
     * @param outerRadius Outer point radius
     * @param innerRadius Inner notch radius
     * @param points Number of star points
     * @param rotation Rotation angle in radians
     * @param color Fill color
     */
    void drawStar(uint16_t* buffer, int16_t cx, int16_t cy,
                  int16_t outerRadius, int16_t innerRadius,
                  int points, float rotation, uint16_t color);

    /**
     * @brief Draw a heart shape
     *
     * Renders a classic heart shape for love/affection expressions.
     * Size can be animated using the scale parameter.
     *
     * @param buffer Target pixel buffer
     * @param cx Center X
     * @param cy Center Y
     * @param size Base size of the heart
     * @param color Fill color
     */
    void drawHeart(uint16_t* buffer, int16_t cx, int16_t cy,
                   int16_t size, uint16_t color);

    /**
     * @brief Draw a swirl/spiral shape
     *
     * Renders a spiral pattern for confusion/dizziness.
     * Rotation animates the spiral.
     *
     * @param buffer Target pixel buffer
     * @param cx Center X
     * @param cy Center Y
     * @param size Spiral size
     * @param rotation Rotation angle in radians
     * @param color Fill color
     */
    void drawSwirl(uint16_t* buffer, int16_t cx, int16_t cy,
                   int16_t size, float rotation, uint16_t color);
};

#endif // EYE_RENDERER_H
