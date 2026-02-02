/**
 * @file eye_shape.h
 * @brief Parametric eye shape definition for expressive robot eyes
 *
 * This file defines the EyeShape structure which controls all visual aspects of
 * a single eye. The parametric approach allows smooth interpolation between any
 * two eye states, enabling fluid expression transitions and animations.
 *
 * COORDINATE SYSTEM NOTE:
 * The display is physically rotated 90° clockwise. This means:
 *   - Buffer X axis → Screen vertical (top to bottom)
 *   - Buffer Y axis → Screen horizontal (left to right)
 *   - "Width" in buffer appears as height on screen
 *   - "Height" in buffer appears as width on screen
 *
 * All shape parameters use normalized values for easy interpolation:
 *   - Multipliers: 1.0 = default size, <1.0 = smaller, >1.0 = larger
 *   - Offsets: -1.0 to 1.0 range, mapped to pixel values
 *   - Lid closure: 0.0 = open, 1.0 = closed
 *
 * @author Robot Eyes Project
 * @date 2025
 */

#ifndef EYE_SHAPE_H
#define EYE_SHAPE_H

#include <Arduino.h>

//-----------------------------------------------------------------------------
// Base Dimensions (in pixels)
//-----------------------------------------------------------------------------

/** Default eye width in pixels (appears as height on rotated screen) */
#define BASE_EYE_WIDTH   120

/** Default eye height in pixels (appears as width on rotated screen) */
#define BASE_EYE_HEIGHT  100

/** Default corner radius for rounded rectangle shape */
#define BASE_CORNER_RADIUS 25

//-----------------------------------------------------------------------------
// Shape Types
//-----------------------------------------------------------------------------

/**
 * @enum ShapeType
 * @brief Fundamental eye shape geometry
 *
 * Different shape types use completely different rendering algorithms.
 * Transitions between shape types use crossfade (opacity blending).
 */
enum class ShapeType {
    Rectangle,  ///< Standard rounded rectangle (default)
    Star,       ///< Star shape for dizzy/knocked expressions
    Heart,      ///< Heart shape for love/affection
    Swirl,      ///< Spiral for confusion/dizziness
    Circle      ///< Perfect circle (simplified startled)
};

//-----------------------------------------------------------------------------
// EyeShape Structure
//-----------------------------------------------------------------------------

/**
 * @struct EyeShape
 * @brief Complete parametric definition of an eye's visual appearance
 *
 * This structure contains all parameters needed to render an expressive eye.
 * Parameters are designed for smooth interpolation, allowing fluid transitions
 * between expressions using the static lerp() function.
 *
 * Example usage:
 * @code
 *   EyeShape happy;
 *   happy.height = 0.8f;        // Slightly squished
 *   happy.outerCornerY = 0.2f;  // Raised outer corners (smile)
 *
 *   EyeShape current = EyeShape::lerp(neutral, happy, 0.5f);  // 50% transition
 * @endcode
 */
struct EyeShape {
    //-------------------------------------------------------------------------
    // Base Shape Parameters
    //-------------------------------------------------------------------------

    /**
     * Eye width multiplier (0.5-1.5 typical range)
     * 1.0 = BASE_EYE_WIDTH pixels. Due to 90° rotation, this controls
     * the vertical extent of the eye as seen on screen.
     */
    float width;

    /**
     * Eye height multiplier (0.5-1.5 typical range)
     * 1.0 = BASE_EYE_HEIGHT pixels. Due to 90° rotation, this controls
     * the horizontal extent of the eye as seen on screen.
     */
    float height;

    /**
     * Corner roundness multiplier (0.0-2.0 typical range)
     * 0.0 = sharp corners, 1.0 = default rounding, 2.0 = very round
     * Higher values approach circular shape for startled expressions.
     */
    float cornerRadius;

    //-------------------------------------------------------------------------
    // Position Offsets (Gaze Direction)
    //-------------------------------------------------------------------------

    /**
     * Horizontal gaze offset (-1.0 to 1.0)
     * Maps to ±50 pixels. Controls where the eye "looks" horizontally.
     * Due to rotation: negative = look up, positive = look down on screen.
     */
    float offsetX;

    /**
     * Vertical gaze offset (-1.0 to 1.0)
     * Maps to ±60 pixels. Controls where the eye "looks" vertically.
     * Due to rotation: negative = look left, positive = look right on screen.
     */
    float offsetY;

    //-------------------------------------------------------------------------
    // Eyelid Parameters
    //-------------------------------------------------------------------------

    /**
     * Top eyelid closure (0.0-1.0)
     * 0.0 = fully open, 1.0 = fully closed
     * Used for blinking, sleepy expressions, and angry "heavy brow" looks.
     */
    float topLid;

    /**
     * Bottom eyelid closure (0.0-1.0)
     * 0.0 = fully open, 1.0 = fully closed
     * Less commonly used; creates squinting effect when combined with topLid.
     */
    float bottomLid;

    //-------------------------------------------------------------------------
    // Corner Shape Modifiers
    //-------------------------------------------------------------------------

    /**
     * Inner corner vertical offset (-1.0 to 1.0)
     * Positive = raise inner corner (angry brow), negative = lower (sad)
     * Maps to ±15 pixels of vertical displacement.
     */
    float innerCornerY;

    /**
     * Outer corner vertical offset (-1.0 to 1.0)
     * Positive = raise outer corner (happy), negative = lower (sad/droopy)
     * Maps to ±15 pixels of vertical displacement.
     */
    float outerCornerY;

    //-------------------------------------------------------------------------
    // Squash and Stretch
    //-------------------------------------------------------------------------

    /**
     * Vertical compression multiplier
     * < 1.0 = squashed (joyful), 1.0 = normal, > 1.0 = stretched
     */
    float squash;

    /**
     * Horizontal compression multiplier
     * < 1.0 = stretched horizontally, 1.0 = normal
     */
    float stretch;

    //-------------------------------------------------------------------------
    // Animation Parameters
    //-------------------------------------------------------------------------

    /**
     * Overall eye openness (0.0-1.0)
     * Primary blink animation parameter.
     * 0.0 = eye closed (thin line), 1.0 = fully open
     * Affects width dimension which appears vertical on rotated screen.
     */
    float openness;

    //-------------------------------------------------------------------------
    // Advanced Shape Modifiers
    //-------------------------------------------------------------------------

    /**
     * Top edge pinch factor (0.0-1.0)
     * 0.0 = normal flat top, 1.0 = pinched to a point
     * Used for "> <" yawn expression where eyes become diamond-shaped.
     */
    float topPinch;

    /**
     * Bottom edge pinch factor (0.0-1.0)
     * 0.0 = normal flat bottom, 1.0 = pinched to a point
     * Combined with topPinch creates the tight squeeze yawn look.
     */
    float bottomPinch;

    /**
     * Top edge curve factor (0.0-1.0)
     * 0.0 = flat edge, 1.0 = deeply curved inward
     * Creates smile-line or crescent shapes for content expressions.
     */
    float topCurve;

    /**
     * Bottom edge curve factor (0.0-1.0)
     * 0.0 = flat edge, 1.0 = deeply curved inward
     * Creates half-moon arch shapes when combined with topLid.
     */
    float bottomCurve;

    //-------------------------------------------------------------------------
    // Shape Type
    //-------------------------------------------------------------------------

    /**
     * Fundamental shape geometry type.
     * Different shapes use different rendering algorithms.
     * Rectangle is the default for most expressions.
     */
    ShapeType shapeType;

    /**
     * Shape blend factor for crossfade transitions (0.0-1.0)
     * Used when transitioning between different shape types.
     * 0.0 = fully current shape, 1.0 = fully target shape
     */
    float shapeBlend;

    /**
     * Animation phase for animated shapes like stars/swirls (0.0-1.0)
     * Increments over time for rotation/pulsing effects.
     */
    float animPhase;

    /**
     * Number of points for star shape (3-8 typical)
     */
    int starPoints;

    //-------------------------------------------------------------------------
    // Constructor
    //-------------------------------------------------------------------------

    /**
     * @brief Default constructor initializes to neutral expression
     *
     * Creates a standard eye shape with:
     * - Normal size (all multipliers = 1.0)
     * - No position offset (looking straight ahead)
     * - Eyelids fully open
     * - No corner modifications
     * - No pinch or curve effects
     */
    EyeShape() :
        width(1.0f),
        height(1.0f),
        cornerRadius(1.0f),
        offsetX(0.0f),
        offsetY(0.0f),
        topLid(0.0f),
        bottomLid(0.0f),
        innerCornerY(0.0f),
        outerCornerY(0.0f),
        squash(1.0f),
        stretch(1.0f),
        openness(1.0f),
        topPinch(0.0f),
        bottomPinch(0.0f),
        topCurve(0.0f),
        bottomCurve(0.0f),
        shapeType(ShapeType::Rectangle),
        shapeBlend(0.0f),
        animPhase(0.0f),
        starPoints(5) {}

    //-------------------------------------------------------------------------
    // Pixel Dimension Accessors
    //-------------------------------------------------------------------------

    /**
     * @brief Calculate actual eye width in pixels
     * @return Width accounting for multipliers and openness (for blink)
     * @note Due to 90° rotation, this appears as vertical size on screen
     */
    int16_t getWidth() const {
        return (int16_t)(BASE_EYE_WIDTH * width * stretch * openness);
    }

    /**
     * @brief Calculate actual eye height in pixels
     * @return Height accounting for multipliers
     * @note Due to 90° rotation, this appears as horizontal size on screen
     */
    int16_t getHeight() const {
        return (int16_t)(BASE_EYE_HEIGHT * height * squash);
    }

    /**
     * @brief Calculate corner radius in pixels
     * @return Radius clamped to half of current height
     *
     * The radius is clamped to prevent visual artifacts when the eye
     * is partially closed during blink animations.
     */
    int16_t getCornerRadius() const {
        int16_t r = (int16_t)(BASE_CORNER_RADIUS * cornerRadius);
        int16_t maxR = getHeight() / 2;
        return min(r, maxR);
    }

    /**
     * @brief Convert normalized X offset to pixels
     * @return Pixel offset for buffer X axis (screen vertical)
     */
    int16_t getOffsetXPixels() const {
        return (int16_t)(offsetX * 126.0f);
    }

    /**
     * @brief Convert normalized Y offset to pixels
     * @return Pixel offset for buffer Y axis (screen horizontal)
     */
    int16_t getOffsetYPixels() const {
        return (int16_t)(offsetY * 112.0f);
    }

    //-------------------------------------------------------------------------
    // Interpolation
    //-------------------------------------------------------------------------

    /**
     * @brief Linear interpolation between two eye shapes
     * @param a Starting shape (t=0)
     * @param b Ending shape (t=1)
     * @param t Interpolation factor (0.0-1.0)
     * @return New EyeShape with all parameters interpolated
     *
     * This function enables smooth transitions between any two expressions.
     * All parameters are interpolated linearly, which works well for most
     * animation purposes.
     *
     * Example:
     * @code
     *   // Animate from neutral to happy over time
     *   float t = animationProgress;  // 0.0 to 1.0
     *   EyeShape current = EyeShape::lerp(neutral, happy, t);
     * @endcode
     */
    static EyeShape lerp(const EyeShape& a, const EyeShape& b, float t) {
        EyeShape result;
        result.width = a.width + (b.width - a.width) * t;
        result.height = a.height + (b.height - a.height) * t;
        result.cornerRadius = a.cornerRadius + (b.cornerRadius - a.cornerRadius) * t;
        result.offsetX = a.offsetX + (b.offsetX - a.offsetX) * t;
        result.offsetY = a.offsetY + (b.offsetY - a.offsetY) * t;
        result.topLid = a.topLid + (b.topLid - a.topLid) * t;
        result.bottomLid = a.bottomLid + (b.bottomLid - a.bottomLid) * t;
        result.innerCornerY = a.innerCornerY + (b.innerCornerY - a.innerCornerY) * t;
        result.outerCornerY = a.outerCornerY + (b.outerCornerY - a.outerCornerY) * t;
        result.squash = a.squash + (b.squash - a.squash) * t;
        result.stretch = a.stretch + (b.stretch - a.stretch) * t;
        result.openness = a.openness + (b.openness - a.openness) * t;
        result.topPinch = a.topPinch + (b.topPinch - a.topPinch) * t;
        result.bottomPinch = a.bottomPinch + (b.bottomPinch - a.bottomPinch) * t;
        result.topCurve = a.topCurve + (b.topCurve - a.topCurve) * t;
        result.bottomCurve = a.bottomCurve + (b.bottomCurve - a.bottomCurve) * t;
        // Shape type uses target when t > 0.5 for crossfade
        result.shapeType = (t < 0.5f) ? a.shapeType : b.shapeType;
        result.shapeBlend = a.shapeBlend + (b.shapeBlend - a.shapeBlend) * t;
        result.animPhase = a.animPhase + (b.animPhase - a.animPhase) * t;
        result.starPoints = (t < 0.5f) ? a.starPoints : b.starPoints;
        return result;
    }
};

#endif // EYE_SHAPE_H
