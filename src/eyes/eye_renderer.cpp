/**
 * @file eye_renderer.cpp
 * @brief Implementation of the parametric eye renderer
 *
 * This file implements the software rendering pipeline for robot eyes.
 * The rendering uses per-pixel evaluation to support complex shape modifications
 * that wouldn't be possible with standard graphics primitives.
 *
 * RENDERING PIPELINE:
 * 1. Calculate eye dimensions from EyeShape parameters
 * 2. Draw base rounded rectangle with pinch/curve effects
 * 3. Apply crescent subtraction for curved expressions (optional)
 * 4. Apply eyelid masks for blink and drowsy effects
 *
 * PERFORMANCE NOTES:
 * - Full buffer scan is O(width * height) per eye
 * - Early-exit optimization skips pixels far outside eye bounds
 * - At 60fps with dual eyes, this renders ~29M pixels/second
 *
 * @author Robot Eyes Project
 * @date 2025
 */

#include "eye_renderer.h"
#include <cmath>

//=============================================================================
// Constructor
//=============================================================================

/**
 * @brief Initialize renderer with default buffer dimensions
 */
EyeRenderer::EyeRenderer()
    : curBufWidth(EYE_BUF_WIDTH)
    , curBufHeight(EYE_BUF_HEIGHT)
    , eyeColor(DEFAULT_EYE_COLOR) {
}

//=============================================================================
// Buffer Management
//=============================================================================

/**
 * @brief Clear buffer using default dimensions
 */
void EyeRenderer::clearBuffer(uint16_t* buffer) {
    clearBuffer(buffer, EYE_BUF_WIDTH, EYE_BUF_HEIGHT);
}

/**
 * @brief Fill entire buffer with background color
 *
 * Simple linear fill - could be optimized with memset for black background
 * but kept readable for maintainability.
 */
void EyeRenderer::clearBuffer(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight) {
    // BG_COLOR is 0x0000, so memset with 0 is valid and much faster
    memset(buffer, 0, (size_t)bufWidth * bufHeight * sizeof(uint16_t));
}

//=============================================================================
// Public Render Interface
//=============================================================================

/**
 * @brief Legacy render function using default buffer size
 */
void EyeRenderer::render(const EyeShape& shape, uint16_t* buffer,
                         int16_t centerX, int16_t centerY, bool isLeftEye) {
    renderToBuf(shape, buffer, EYE_BUF_WIDTH, EYE_BUF_HEIGHT, centerX, centerY, isLeftEye, true);
}

/**
 * @brief Main rendering function - converts EyeShape to pixels
 *
 * This is the primary entry point for eye rendering. It orchestrates the
 * complete rendering pipeline:
 *
 * 1. SETUP: Configure buffer dimensions and calculate pixel sizes
 * 2. SHAPE: Draw the main eye shape with all geometric modifiers
 * 3. CURVES: Apply crescent subtraction for curved expressions
 * 4. LIDS: Apply eyelid masks for blink animations
 *
 * The rendering respects the 90° screen rotation by mapping buffer coordinates
 * appropriately to screen coordinates.
 */
void EyeRenderer::renderToBuf(const EyeShape& shape, uint16_t* buffer,
                               int16_t bufWidth, int16_t bufHeight,
                               int16_t centerX, int16_t centerY,
                               bool isLeftEye, bool clearFirst) {
    // Store current buffer dimensions for use by private methods
    curBufWidth = bufWidth;
    curBufHeight = bufHeight;

    // Optionally clear buffer (skip when rendering multiple eyes to same buffer)
    if (clearFirst) {
        clearBuffer(buffer, bufWidth, bufHeight);
    }

    //-------------------------------------------------------------------------
    // Calculate Pixel Dimensions
    //-------------------------------------------------------------------------

    // Get actual pixel dimensions from normalized shape parameters
    int16_t eyeWidth = shape.getWidth();
    int16_t eyeHeight = shape.getHeight();
    int16_t radius = shape.getCornerRadius();

    // Enforce minimum width to prevent rendering artifacts during blink
    // When nearly closed, eye should be a thin line, not disappear entirely
    if (eyeWidth < 4) eyeWidth = 4;

    // Convert normalized gaze offset to pixels
    int16_t offsetX = shape.getOffsetXPixels();
    int16_t offsetY = shape.getOffsetYPixels();

    // Calculate top-left corner of eye bounding box in buffer coordinates
    int16_t eyeX = centerX - eyeWidth / 2 + offsetX;
    int16_t eyeY = centerY - eyeHeight / 2 + offsetY;

    // Clamp eye position to keep entirely within buffer bounds
    if (eyeX < 0) { offsetX -= eyeX; eyeX = 0; }
    if (eyeY < 0) { offsetY -= eyeY; eyeY = 0; }
    if (eyeX + eyeWidth > bufWidth) { int16_t excess = eyeX + eyeWidth - bufWidth; offsetX -= excess; eyeX -= excess; }
    if (eyeY + eyeHeight > bufHeight) { int16_t excess = eyeY + eyeHeight - bufHeight; offsetY -= excess; eyeY -= excess; }

    //-------------------------------------------------------------------------
    // Draw Main Eye Shape Based on ShapeType
    //-------------------------------------------------------------------------

    // Calculate center position for alternative shapes
    int16_t shapeCenterX = centerX + offsetX;
    int16_t shapeCenterY = centerY + offsetY;

    switch (shape.shapeType) {
        case ShapeType::Star: {
            // Star shape for dizzy/knocked expressions
            int16_t outerR = (int16_t)(eyeHeight * 0.6f);
            int16_t innerR = (int16_t)(outerR * 0.4f);
            drawStar(buffer, shapeCenterX, shapeCenterY,
                     outerR, innerR, shape.starPoints,
                     shape.animPhase * 2.0f * M_PI, eyeColor);
            return;  // Stars don't use eyelids
        }

        case ShapeType::Heart: {
            // Heart shape for love expressions
            int16_t heartSize = (int16_t)(eyeHeight * 0.5f);
            drawHeart(buffer, shapeCenterX, shapeCenterY, heartSize, eyeColor);
            return;  // Hearts don't use eyelids
        }

        case ShapeType::Swirl: {
            // Swirl shape for confusion/dizziness (random rotation per eye)
            int16_t swirlSize = (int16_t)(eyeHeight * 0.6f);
            // Different rotation for each eye to avoid symmetry
            float rotation = isLeftEye ? 0.3f : -0.5f;
            drawSwirl(buffer, shapeCenterX, shapeCenterY, swirlSize,
                      rotation, eyeColor);
            return;  // Swirls don't use eyelids
        }

        case ShapeType::Circle: {
            // Perfect circle
            int16_t circleR = (int16_t)(eyeHeight * 0.5f);
            drawFilledCircle(buffer, shapeCenterX, shapeCenterY, circleR, eyeColor);
            // Circles can have eyelids, fall through to lid code
            eyeX = shapeCenterX - circleR;
            eyeWidth = circleR * 2;
            eyeY = shapeCenterY - circleR;
            eyeHeight = circleR * 2;
            break;
        }

        case ShapeType::Rectangle:
        default: {
            // Standard rounded rectangle with all geometric modifiers
            drawRoundedRect(buffer, eyeX, eyeY, eyeWidth, eyeHeight, radius,
                            shape.innerCornerY, shape.outerCornerY,
                            shape.topPinch, shape.bottomPinch,
                            shape.topCurve, shape.bottomCurve,
                            isLeftEye);

            //---------------------------------------------------------------------
            // Apply Crescent Effects (Subtractive Circles) - Rectangle only
            //---------------------------------------------------------------------

            // For strong curve values, use subtractive circles to create crescent shapes.
            if (shape.bottomCurve > 0.3f) {
                int16_t circleRadius = (int16_t)(eyeHeight * 3.0f);
                int16_t circleCenterX = eyeX + eyeWidth + circleRadius -
                                         (int16_t)(eyeHeight * shape.bottomCurve * 0.6f);
                int16_t circleCenterY = centerY + offsetY;
                drawFilledCircle(buffer, circleCenterX, circleCenterY, circleRadius, BG_COLOR);
            }

            if (shape.topCurve > 0.3f) {
                int16_t circleRadius = (int16_t)(eyeHeight * 3.0f);
                int16_t circleCenterX = eyeX - circleRadius +
                                         (int16_t)(eyeHeight * shape.topCurve * 0.6f);
                int16_t circleCenterY = centerY + offsetY;
                drawFilledCircle(buffer, circleCenterX, circleCenterY, circleRadius, BG_COLOR);
            }
            break;
        }
    }

    //-------------------------------------------------------------------------
    // Apply Eyelid Masks (Rectangle and Circle shapes)
    //-------------------------------------------------------------------------

    if (shape.topLid > 0.0f) {
        applyTopLid(buffer, eyeX, eyeWidth, shape.topLid, eyeY, eyeHeight);
    }

    if (shape.bottomLid > 0.0f) {
        applyBottomLid(buffer, eyeX + eyeWidth, eyeWidth, shape.bottomLid, eyeY, eyeHeight);
    }
}

//=============================================================================
// Shape Rendering
//=============================================================================

/**
 * @brief Draw rounded rectangle with advanced shape modifications
 *
 * This is the core shape rendering function. It uses per-pixel evaluation
 * rather than graphics primitives, which allows for:
 *
 * - Corner Y offsets that skew the rectangle
 * - Pinch effects that narrow edges to points
 * - Curve effects that bend edges inward
 * - Smooth corner rounding that adapts to shape modifications
 *
 * ALGORITHM:
 * For each pixel in the buffer:
 * 1. Calculate position relative to eye bounding box
 * 2. Apply curve offsets to X bounds (creates crescent shapes)
 * 3. Apply pinch to Y bounds (creates diamond shapes)
 * 4. Apply corner Y offset (creates expression tilt)
 * 5. Check if pixel is inside the modified shape
 * 6. Apply corner rounding
 *
 * @param buffer Target pixel buffer
 * @param x,y Top-left corner of bounding box
 * @param w,h Dimensions of eye shape
 * @param r Corner radius
 * @param innerCornerY Vertical offset for inner corner edge
 * @param outerCornerY Vertical offset for outer corner edge
 * @param topPinch Factor to narrow top edge (0-1)
 * @param bottomPinch Factor to narrow bottom edge (0-1)
 * @param topCurve Factor to curve top edge inward (0-1)
 * @param bottomCurve Factor to curve bottom edge inward (0-1)
 * @param isLeftEye Used for asymmetric expressions
 */
void EyeRenderer::drawRoundedRect(uint16_t* buffer, int16_t x, int16_t y,
                                   int16_t w, int16_t h, int16_t r,
                                   float innerCornerY, float outerCornerY,
                                   float topPinch, float bottomPinch,
                                   float topCurve, float bottomCurve,
                                   bool isLeftEye) {
    // Clamp corner radius to prevent overlap (radius can't exceed half of width or height)
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    // Convert normalized corner offsets to pixel values
    // Range is approximately ±15 pixels for full -1 to 1 range
    //
    // IMPORTANT: "Inner" means toward the nose (between eyes), "outer" means away from nose
    // With 90° rotation, buffer Y maps to screen horizontal:
    //   - Buffer Y=0 (normalizedY=0) = screen LEFT
    //   - Buffer Y=h (normalizedY=1) = screen RIGHT
    //
    // For LEFT eye on screen:  inner=RIGHT side, outer=LEFT side → swap offsets
    // For RIGHT eye on screen: inner=LEFT side, outer=RIGHT side → normal order
    float innerOffset = innerCornerY * 15.0f;
    float outerOffset = outerCornerY * 15.0f;
    if (isLeftEye) {
        // Swap inner/outer for left eye since its outer corner is on screen left
        float temp = innerOffset;
        innerOffset = outerOffset;
        outerOffset = temp;
    }

    // Determine if we need to use the complex geometry path
    bool hasPinch = (topPinch > 0.001f || bottomPinch > 0.001f);
    bool hasCurve = (topCurve > 0.001f || bottomCurve > 0.001f);

    //-------------------------------------------------------------------------
    // Per-Pixel Scanline Rendering
    //-------------------------------------------------------------------------

    for (int16_t py = 0; py < curBufHeight; py++) {
        for (int16_t px = 0; px < curBufWidth; px++) {
            // Calculate position relative to eye bounding box origin
            int16_t rx = px - x;
            int16_t ry = py - y;

            // Early exit: skip pixels clearly outside the bounding box
            // Allow margin for corner offsets and modifications
            if (rx < -20 || rx >= w + 20 || ry < -20 || ry >= h + 20) continue;

            // Normalize position to 0-1 range within eye bounds
            // Clamped to handle positions outside the basic rectangle
            float normalizedX = (w > 0) ? constrain((float)rx / (float)w, 0.0f, 1.0f) : 0.0f;
            float normalizedY = (h > 0) ? constrain((float)ry / (float)h, 0.0f, 1.0f) : 0.0f;

            //------------------------------------------------------------------
            // Calculate Curve Effect (affects X bounds)
            //------------------------------------------------------------------

            // Curve creates crescent/half-moon shapes by pushing edges inward
            // at the vertical extremes (top and bottom of eye in screen coords)
            //
            // Uses inverted parabola: maximum effect at edges (ny=0,1), zero at center (ny=0.5)
            float parabola = 4.0f * normalizedY * (1.0f - normalizedY);  // 0 → 1 → 0
            float curveShapeY = 1.0f - parabola;                          // 1 → 0 → 1

            // Apply curve to left and right edges
            float leftEdgeOffset = topCurve * curveShapeY * (w * 0.5f);
            float rightEdgeOffset = bottomCurve * curveShapeY * (w * 0.5f);

            //------------------------------------------------------------------
            // Calculate Pinch Effect (affects Y bounds)
            //------------------------------------------------------------------

            // Pinch narrows the eye at left/right extremes, creating pointed tips
            // Used for the "> <" yawn shape
            float distFromCenterX = fabsf(normalizedX - 0.5f) * 2.0f;

            // Interpolate pinch amount based on horizontal position
            float pinchAmount = (normalizedX < 0.5f)
                ? topPinch * (1.0f - normalizedX * 2.0f)       // Left half
                : bottomPinch * ((normalizedX - 0.5f) * 2.0f); // Right half

            // Calculate height scale for this column
            float heightScale = 1.0f - pinchAmount * distFromCenterX;
            if (heightScale < 0.02f) heightScale = 0.02f;  // Prevent zero height

            // Calculate adjusted Y bounds for this column
            float colHeight = h * heightScale;
            float colTop = (h - colHeight) / 2.0f;
            float colBottom = colTop + colHeight;

            //------------------------------------------------------------------
            // Apply Corner Y Offset
            //------------------------------------------------------------------

            // Corner Y offset skews the eye shape for expressions
            // Interpolate between inner and outer offset based on Y position
            // NOTE: Despite the name "Y", after 90° rotation these offsets need to be
            // applied to X (buffer vertical) to move corners up/down on screen
            float rowYOffset = innerOffset * (1.0f - normalizedY) + outerOffset * normalizedY;

            // Apply offset to X coordinate (screen vertical after rotation)
            float adjustedRX = (float)rx - rowYOffset;
            float adjustedRY = (float)ry;

            // Calculate effective X bounds with curve applied
            float effectiveLeft = leftEdgeOffset;
            float effectiveRight = (float)w - rightEdgeOffset;

            //------------------------------------------------------------------
            // Inside/Outside Test
            //------------------------------------------------------------------

            bool inside = false;

            if (hasPinch || hasCurve) {
                // Complex geometry path: use modified bounds
                inside = (adjustedRX >= effectiveLeft && adjustedRX < effectiveRight &&
                         adjustedRY >= colTop && adjustedRY < colBottom);

                // Apply corner rounding (reduced when heavily pinched)
                if (inside && r > 0 && pinchAmount < 0.5f) {
                    float effectiveR = r * (1.0f - pinchAmount);
                    float adjW = effectiveRight - effectiveLeft;
                    float adjH = colBottom - colTop;

                    // Position within the modified shape
                    float localX = adjustedRX - effectiveLeft;
                    float localY = adjustedRY - colTop;

                    // Check all four corners for rounding
                    if (localX < effectiveR && localY < effectiveR) {
                        // Top-left corner
                        float dx = effectiveR - localX;
                        float dy = effectiveR - localY;
                        if (dx * dx + dy * dy > effectiveR * effectiveR) inside = false;
                    } else if (localX >= adjW - effectiveR && localY < effectiveR) {
                        // Top-right corner
                        float dx = localX - (adjW - effectiveR);
                        float dy = effectiveR - localY;
                        if (dx * dx + dy * dy > effectiveR * effectiveR) inside = false;
                    } else if (localX < effectiveR && localY >= adjH - effectiveR) {
                        // Bottom-left corner
                        float dx = effectiveR - localX;
                        float dy = localY - (adjH - effectiveR);
                        if (dx * dx + dy * dy > effectiveR * effectiveR) inside = false;
                    } else if (localX >= adjW - effectiveR && localY >= adjH - effectiveR) {
                        // Bottom-right corner
                        float dx = localX - (adjW - effectiveR);
                        float dy = localY - (adjH - effectiveR);
                        if (dx * dx + dy * dy > effectiveR * effectiveR) inside = false;
                    }
                }
            } else {
                // Simple geometry path: standard rounded rectangle
                if (adjustedRX >= 0 && adjustedRX < w && adjustedRY >= 0 && adjustedRY < h) {
                    inside = true;

                    // Check corners for rounding
                    if (rx < r && adjustedRY < r) {
                        int16_t dx = r - rx - 1;
                        int16_t dy = r - (int16_t)adjustedRY - 1;
                        if (dx * dx + dy * dy > r * r) inside = false;
                    } else if (rx >= w - r && adjustedRY < r) {
                        int16_t dx = rx - (w - r);
                        int16_t dy = r - (int16_t)adjustedRY - 1;
                        if (dx * dx + dy * dy > r * r) inside = false;
                    } else if (rx < r && adjustedRY >= h - r) {
                        int16_t dx = r - rx - 1;
                        int16_t dy = (int16_t)adjustedRY - (h - r);
                        if (dx * dx + dy * dy > r * r) inside = false;
                    } else if (rx >= w - r && adjustedRY >= h - r) {
                        int16_t dx = rx - (w - r);
                        int16_t dy = (int16_t)adjustedRY - (h - r);
                        if (dx * dx + dy * dy > r * r) inside = false;
                    }
                }
            }

            // Set pixel color if inside the eye shape
            if (inside) {
                buffer[py * curBufWidth + px] = eyeColor;
            }
        }
    }
}

//=============================================================================
// Eyelid Rendering
//=============================================================================

/**
 * @brief Apply top eyelid mask (appears as top on rotated screen)
 *
 * Fills pixels from the left side of the buffer (which appears as the top
 * of the screen after 90° rotation) with background color.
 *
 * Only modifies pixels that are already eyeColor to preserve the rounded
 * corners of the eye shape.
 */
void EyeRenderer::applyTopLid(uint16_t* buffer, int16_t eyeLeft, int16_t eyeWidth,
                               float lidAmount, int16_t eyeY, int16_t eyeHeight) {
    // Calculate how many pixels of the eye width to cover
    int16_t lidPixels = (int16_t)(eyeWidth * lidAmount);
    if (lidPixels <= 0) return;

    // Fill from left edge moving rightward
    for (int16_t px = eyeLeft; px < eyeLeft + lidPixels && px < curBufWidth; px++) {
        if (px < 0) continue;

        for (int16_t py = eyeY; py < eyeY + eyeHeight && py < curBufHeight; py++) {
            if (py < 0) continue;

            // Only clear pixels that are part of the eye (preserves corners)
            if (buffer[py * curBufWidth + px] == eyeColor) {
                buffer[py * curBufWidth + px] = BG_COLOR;
            }
        }
    }
}

/**
 * @brief Apply bottom eyelid mask (appears as bottom on rotated screen)
 *
 * Fills pixels from the right side of the buffer (which appears as the bottom
 * of the screen after 90° rotation) with background color.
 */
void EyeRenderer::applyBottomLid(uint16_t* buffer, int16_t eyeRight, int16_t eyeWidth,
                                  float lidAmount, int16_t eyeY, int16_t eyeHeight) {
    // Calculate how many pixels of the eye width to cover
    int16_t lidPixels = (int16_t)(eyeWidth * lidAmount);
    if (lidPixels <= 0) return;

    // Fill from right edge moving leftward
    for (int16_t px = eyeRight - lidPixels; px < eyeRight && px < curBufWidth; px++) {
        if (px < 0) continue;

        for (int16_t py = eyeY; py < eyeY + eyeHeight && py < curBufHeight; py++) {
            if (py < 0) continue;

            // Only clear pixels that are part of the eye (preserves corners)
            if (buffer[py * curBufWidth + px] == eyeColor) {
                buffer[py * curBufWidth + px] = BG_COLOR;
            }
        }
    }
}

//=============================================================================
// Utility Drawing Functions
//=============================================================================

/**
 * @brief Draw a filled circle using distance-based evaluation
 *
 * Used primarily for creating crescent shapes by subtracting large circles
 * from the eye. The circle is filled with the specified color (typically
 * BG_COLOR for subtraction effects).
 *
 * Uses simple distance-squared comparison for efficiency (avoids sqrt).
 */
void EyeRenderer::drawFilledCircle(uint16_t* buffer, int16_t cx, int16_t cy,
                                    int16_t radius, uint16_t color) {
    int16_t r2 = radius * radius;

    // Iterate over bounding box of circle
    for (int16_t py = cy - radius; py <= cy + radius; py++) {
        if (py < 0 || py >= curBufHeight) continue;

        for (int16_t px = cx - radius; px <= cx + radius; px++) {
            if (px < 0 || px >= curBufWidth) continue;

            // Distance-squared test (avoids expensive sqrt)
            int16_t dx = px - cx;
            int16_t dy = py - cy;
            if (dx * dx + dy * dy <= r2) {
                buffer[py * curBufWidth + px] = color;
            }
        }
    }
}

//=============================================================================
// Star Shape Rendering
//=============================================================================

/**
 * @brief Draw a filled star shape
 *
 * Uses polar coordinates to determine if a point is inside the star.
 * The radius alternates between outer (points) and inner (notches).
 */
void EyeRenderer::drawStar(uint16_t* buffer, int16_t cx, int16_t cy,
                           int16_t outerRadius, int16_t innerRadius,
                           int points, float rotation, uint16_t color) {
    // Iterate over bounding box
    for (int16_t py = cy - outerRadius; py <= cy + outerRadius; py++) {
        if (py < 0 || py >= curBufHeight) continue;

        for (int16_t px = cx - outerRadius; px <= cx + outerRadius; px++) {
            if (px < 0 || px >= curBufWidth) continue;

            // Convert to polar coordinates
            // NOTE: Buffer is rotated 90° from screen, so swap X/Y for screen-space shape
            // Buffer X (px) → screen vertical, Buffer Y (py) → screen horizontal
            float dx = (float)(py - cy);  // Screen horizontal
            float dy = (float)(px - cx);  // Screen vertical
            float dist = sqrtf(dx * dx + dy * dy);
            float angle = atan2f(dy, dx) + rotation;

            // Normalize angle to 0-2PI
            while (angle < 0) angle += 2.0f * M_PI;
            while (angle >= 2.0f * M_PI) angle -= 2.0f * M_PI;

            // Calculate star radius at this angle
            // Each point spans PI/points radians
            float segmentAngle = M_PI / (float)points;
            float angleInSegment = fmodf(angle, segmentAngle * 2.0f);

            // Triangle wave between inner and outer radius
            float t = angleInSegment / segmentAngle;
            if (t > 1.0f) t = 2.0f - t;  // Mirror for second half

            float starRadius = innerRadius + (outerRadius - innerRadius) * (1.0f - t);

            if (dist <= starRadius) {
                buffer[py * curBufWidth + px] = color;
            }
        }
    }
}

//=============================================================================
// Heart Shape Rendering
//=============================================================================

/**
 * @brief Draw a filled heart shape
 *
 * Uses the classic heart curve equation:
 * (x² + y² - 1)³ - x²y³ < 0
 */
void EyeRenderer::drawHeart(uint16_t* buffer, int16_t cx, int16_t cy,
                            int16_t size, uint16_t color) {
    // Iterate over bounding box (hearts are taller than wide)
    int16_t halfW = size;
    int16_t halfH = (int16_t)(size * 1.2f);

    for (int16_t py = cy - halfH; py <= cy + halfH; py++) {
        if (py < 0 || py >= curBufHeight) continue;

        for (int16_t px = cx - halfW; px <= cx + halfW; px++) {
            if (px < 0 || px >= curBufWidth) continue;

            // Normalize coordinates to roughly -1.5 to 1.5 range
            // NOTE: Buffer is rotated 90° from screen, so swap X/Y for screen-space shape
            // Buffer X (px) → screen vertical, Buffer Y (py) → screen horizontal
            float nx = (float)(py - cy) / (float)size * 1.3f;  // Screen horizontal
            float ny = (float)(px - cx) / (float)size * 1.3f - 0.3f;  // Screen vertical

            // Flip Y so heart points down (toward screen bottom)
            ny = -ny;

            // Heart equation: (x² + y² - 1)³ - x²y³ < 0
            float x2 = nx * nx;
            float y2 = ny * ny;
            float y3 = y2 * ny;
            float term1 = x2 + y2 - 1.0f;
            float result = term1 * term1 * term1 - x2 * y3;

            if (result < 0) {
                buffer[py * curBufWidth + px] = color;
            }
        }
    }
}

//=============================================================================
// Swirl Shape Rendering
//=============================================================================

/**
 * @brief Draw a spiral/swirl shape
 *
 * Renders an Archimedean spiral with thickness.
 */
void EyeRenderer::drawSwirl(uint16_t* buffer, int16_t cx, int16_t cy,
                            int16_t size, float rotation, uint16_t color) {
    float thickness = size * 0.4f;   // Spiral arm thickness (thicker lines)
    float spiralTightness = 2.5f;    // 2-3 rotations total

    for (int16_t py = cy - size; py <= cy + size; py++) {
        if (py < 0 || py >= curBufHeight) continue;

        for (int16_t px = cx - size; px <= cx + size; px++) {
            if (px < 0 || px >= curBufWidth) continue;

            // NOTE: Buffer is rotated 90° from screen, so swap X/Y for screen-space shape
            float dx = (float)(py - cy);  // Screen horizontal
            float dy = (float)(px - cx);  // Screen vertical
            float dist = sqrtf(dx * dx + dy * dy);
            float angle = atan2f(dy, dx) + rotation;

            // Normalize angle
            while (angle < 0) angle += 2.0f * M_PI;

            // Archimedean spiral: r = a * theta
            // We check if the point is close to any part of the spiral
            float normalizedDist = dist / (float)size;

            // Calculate expected angle for this distance on the spiral
            float expectedAngle = normalizedDist * spiralTightness * 2.0f * M_PI;

            // Check all spiral arms (wrapping)
            bool inside = false;
            for (int arm = 0; arm < (int)(spiralTightness + 1); arm++) {
                float armAngle = expectedAngle - arm * 2.0f * M_PI;
                float angleDiff = fabsf(fmodf(angle - armAngle + 3.0f * M_PI, 2.0f * M_PI) - M_PI);

                // Convert angle difference to arc length at this radius
                float arcDist = angleDiff * dist;

                if (arcDist < thickness && dist < size && dist > size * 0.1f) {
                    inside = true;
                    break;
                }
            }

            // Add center dot
            if (dist < size * 0.15f) {
                inside = true;
            }

            if (inside) {
                buffer[py * curBufWidth + px] = color;
            }
        }
    }
}
