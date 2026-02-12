/**
 * Eye Parameters - Defines the shape and appearance of an eye
 * All values normalized (0.0 to 1.0) for resolution independence
 */

#ifndef EYE_PARAMS_H
#define EYE_PARAMS_H

#include <Arduino.h>

/**
 * Parameters that define the shape and position of a single eye
 */
struct EyeParams {
    // Base shape (normalized 0.0-1.0 relative to eye bounding box)
    float width;          // Eye width (1.0 = full width)
    float height;         // Eye height (1.0 = full height)
    float corner_radius;  // Roundness of corners (0.0 = square, 1.0 = fully round)

    // Position offset (normalized -1.0 to 1.0, for gaze direction)
    float x_offset;       // Horizontal position (-1 = left, 0 = center, 1 = right)
    float y_offset;       // Vertical position (-1 = up, 0 = center, 1 = down)

    // Eyelid positions (0.0 = open, 1.0 = fully closed)
    float top_lid;        // Top eyelid closure
    float bottom_lid;     // Bottom eyelid closure

    // Corner slopes (angles in degrees, for expressions)
    float inner_slope;    // Inner corner angle (-45 to 45)
    float outer_slope;    // Outer corner angle (-45 to 45)

    // Scale (1.0 = normal size)
    float scale;

    /**
     * Default neutral eye
     */
    static EyeParams neutral() {
        return {
            .width = 0.9f,
            .height = 0.75f,
            .corner_radius = 0.3f,
            .x_offset = 0.0f,
            .y_offset = 0.0f,
            .top_lid = 0.0f,
            .bottom_lid = 0.0f,
            .inner_slope = 0.0f,
            .outer_slope = 0.0f,
            .scale = 1.0f
        };
    }

    /**
     * Interpolate between two eye parameter sets
     */
    static EyeParams lerp(const EyeParams& a, const EyeParams& b, float t) {
        return {
            .width = a.width + (b.width - a.width) * t,
            .height = a.height + (b.height - a.height) * t,
            .corner_radius = a.corner_radius + (b.corner_radius - a.corner_radius) * t,
            .x_offset = a.x_offset + (b.x_offset - a.x_offset) * t,
            .y_offset = a.y_offset + (b.y_offset - a.y_offset) * t,
            .top_lid = a.top_lid + (b.top_lid - a.top_lid) * t,
            .bottom_lid = a.bottom_lid + (b.bottom_lid - a.bottom_lid) * t,
            .inner_slope = a.inner_slope + (b.inner_slope - a.inner_slope) * t,
            .outer_slope = a.outer_slope + (b.outer_slope - a.outer_slope) * t,
            .scale = a.scale + (b.scale - a.scale) * t
        };
    }
};

/**
 * Configuration for the face (both eyes)
 */
struct FaceConfig {
    // Screen dimensions
    int16_t screen_width;
    int16_t screen_height;

    // Eye positioning
    int16_t eye_spacing;      // Gap between eyes (pixels)
    int16_t eye_width;        // Maximum eye width (pixels)
    int16_t eye_height;       // Maximum eye height (pixels)
    int16_t vertical_offset;  // Y offset from center (pixels)

    // Colors
    uint32_t eye_color;       // Main eye color (RGB)
    uint32_t bg_color;        // Background color (RGB)
    uint32_t lid_color;       // Eyelid color (usually same as bg)

    /**
     * Default config for 448x368 landscape display
     */
    static FaceConfig default_config() {
        return {
            .screen_width = 448,
            .screen_height = 368,
            .eye_spacing = 40,
            .eye_width = 140,
            .eye_height = 180,
            .vertical_offset = 0,
            .eye_color = 0x00FFFF,  // Cyan
            .bg_color = 0x000000,   // Black
            .lid_color = 0x000000   // Black
        };
    }
};

#endif // EYE_PARAMS_H
