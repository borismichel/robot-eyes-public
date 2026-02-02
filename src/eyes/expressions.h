/**
 * Expression Presets - Pre-defined emotional states for the eyes
 */

#ifndef EXPRESSIONS_H
#define EXPRESSIONS_H

#include "eye_params.h"

/**
 * Emotion types available
 */
enum class Emotion {
    NEUTRAL,
    HAPPY,
    SAD,
    SURPRISED,
    ANGRY,
    SUSPICIOUS,
    TIRED,
    EXCITED,
    CONFUSED,
    FOCUSED,
    SHY,
    LOVE,
    DIZZY,
    ANNOYED,
    SCARED,
    SLEEPY,

    COUNT  // Number of emotions
};

/**
 * Expression definition for both eyes
 */
struct Expression {
    EyeParams left;
    EyeParams right;
    bool symmetric;  // If true, right eye mirrors left

    /**
     * Create symmetric expression (same parameters for both eyes)
     */
    static Expression symmetric_expr(const EyeParams& params) {
        return {params, params, true};
    }

    /**
     * Create asymmetric expression (different parameters for each eye)
     */
    static Expression asymmetric_expr(const EyeParams& left, const EyeParams& right) {
        return {left, right, false};
    }
};

/**
 * Get expression preset for an emotion
 */
inline Expression get_expression(Emotion emotion) {
    EyeParams base = EyeParams::neutral();

    switch (emotion) {
        case Emotion::NEUTRAL:
            return Expression::symmetric_expr(base);

        case Emotion::HAPPY: {
            EyeParams happy = base;
            happy.height = 0.5f;        // Squinted
            happy.top_lid = 0.3f;       // Slightly closed
            happy.outer_slope = -15.0f; // Curved up at edges
            happy.corner_radius = 0.5f; // Rounder
            return Expression::symmetric_expr(happy);
        }

        case Emotion::SAD: {
            EyeParams sad = base;
            sad.height = 0.6f;
            sad.inner_slope = 15.0f;    // Droopy inner corners
            sad.outer_slope = 10.0f;    // Droopy outer corners
            sad.y_offset = 0.2f;        // Looking down
            sad.scale = 0.9f;           // Slightly smaller
            return Expression::symmetric_expr(sad);
        }

        case Emotion::SURPRISED: {
            EyeParams surprised = base;
            surprised.height = 1.0f;    // Maximum height
            surprised.width = 1.0f;     // Maximum width
            surprised.corner_radius = 0.4f;
            surprised.scale = 1.1f;     // Slightly larger
            return Expression::symmetric_expr(surprised);
        }

        case Emotion::ANGRY: {
            EyeParams angry = base;
            angry.height = 0.55f;
            angry.inner_slope = -25.0f; // Angled inward (frowning)
            angry.outer_slope = 15.0f;
            angry.top_lid = 0.15f;
            return Expression::symmetric_expr(angry);
        }

        case Emotion::SUSPICIOUS: {
            EyeParams left_sus = base;
            left_sus.height = 0.4f;
            left_sus.top_lid = 0.3f;
            left_sus.inner_slope = -10.0f;

            EyeParams right_sus = left_sus;
            right_sus.height = 0.55f;   // One eye more open
            right_sus.top_lid = 0.1f;

            return Expression::asymmetric_expr(left_sus, right_sus);
        }

        case Emotion::TIRED: {
            EyeParams tired = base;
            tired.height = 0.5f;
            tired.top_lid = 0.4f;       // Heavy eyelids
            tired.y_offset = 0.15f;     // Looking down
            tired.outer_slope = 5.0f;   // Slightly droopy
            return Expression::symmetric_expr(tired);
        }

        case Emotion::EXCITED: {
            EyeParams excited = base;
            excited.height = 0.9f;
            excited.width = 0.95f;
            excited.scale = 1.05f;
            excited.corner_radius = 0.35f;
            return Expression::symmetric_expr(excited);
        }

        case Emotion::CONFUSED: {
            EyeParams left_conf = base;
            left_conf.inner_slope = 10.0f;
            left_conf.outer_slope = -5.0f;

            EyeParams right_conf = base;
            right_conf.height = 0.7f;
            right_conf.inner_slope = -10.0f;

            return Expression::asymmetric_expr(left_conf, right_conf);
        }

        case Emotion::FOCUSED: {
            EyeParams focused = base;
            focused.height = 0.6f;
            focused.width = 0.85f;
            focused.top_lid = 0.2f;
            focused.bottom_lid = 0.1f;
            return Expression::symmetric_expr(focused);
        }

        case Emotion::SHY: {
            EyeParams shy = base;
            shy.height = 0.55f;
            shy.x_offset = 0.3f;        // Looking away
            shy.y_offset = 0.2f;        // Looking down
            shy.top_lid = 0.2f;
            return Expression::symmetric_expr(shy);
        }

        case Emotion::LOVE: {
            // Heart-shaped eyes would need special rendering
            // For now, use happy squinted look
            EyeParams love = base;
            love.height = 0.45f;
            love.corner_radius = 0.6f;
            love.outer_slope = -20.0f;
            love.scale = 1.05f;
            return Expression::symmetric_expr(love);
        }

        case Emotion::DIZZY: {
            EyeParams left_diz = base;
            left_diz.x_offset = -0.2f;
            left_diz.y_offset = 0.1f;

            EyeParams right_diz = base;
            right_diz.x_offset = 0.15f;
            right_diz.y_offset = -0.1f;

            return Expression::asymmetric_expr(left_diz, right_diz);
        }

        case Emotion::ANNOYED: {
            EyeParams annoyed = base;
            annoyed.height = 0.5f;
            annoyed.top_lid = 0.35f;
            annoyed.inner_slope = -15.0f;
            annoyed.x_offset = 0.2f;    // Looking to the side
            return Expression::symmetric_expr(annoyed);
        }

        case Emotion::SCARED: {
            EyeParams scared = base;
            scared.height = 0.95f;
            scared.width = 0.85f;
            scared.y_offset = -0.1f;    // Looking up
            scared.scale = 0.95f;       // Slightly shrunk
            return Expression::symmetric_expr(scared);
        }

        case Emotion::SLEEPY: {
            EyeParams sleepy = base;
            sleepy.height = 0.3f;
            sleepy.top_lid = 0.6f;
            sleepy.outer_slope = 8.0f;
            sleepy.y_offset = 0.25f;
            return Expression::symmetric_expr(sleepy);
        }

        default:
            return Expression::symmetric_expr(base);
    }
}

/**
 * Get emotion name as string
 */
inline const char* emotion_name(Emotion emotion) {
    switch (emotion) {
        case Emotion::NEUTRAL:    return "Neutral";
        case Emotion::HAPPY:      return "Happy";
        case Emotion::SAD:        return "Sad";
        case Emotion::SURPRISED:  return "Surprised";
        case Emotion::ANGRY:      return "Angry";
        case Emotion::SUSPICIOUS: return "Suspicious";
        case Emotion::TIRED:      return "Tired";
        case Emotion::EXCITED:    return "Excited";
        case Emotion::CONFUSED:   return "Confused";
        case Emotion::FOCUSED:    return "Focused";
        case Emotion::SHY:        return "Shy";
        case Emotion::LOVE:       return "Love";
        case Emotion::DIZZY:      return "Dizzy";
        case Emotion::ANNOYED:    return "Annoyed";
        case Emotion::SCARED:     return "Scared";
        case Emotion::SLEEPY:     return "Sleepy";
        default:                  return "Unknown";
    }
}

#endif // EXPRESSIONS_H
