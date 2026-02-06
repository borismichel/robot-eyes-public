/**
 * @file expressions.h
 * @brief Expression presets for expressive robot eyes
 *
 * This file defines a library of emotional expression presets. Each expression
 * is implemented as a function that returns an EyeShape configured for that
 * emotion. Expressions can be smoothly interpolated using EyeShape::lerp().
 *
 * EXPRESSION DESIGN PRINCIPLES:
 * - Each expression should be recognizable at a glance
 * - Parameters should work well with smooth transitions
 * - Asymmetric expressions (Suspicious, Confused) use the isLeftEye parameter
 *
 * USAGE:
 * @code
 *   // Get expression shape for left eye
 *   EyeShape leftEye = getExpressionShape(Expression::Happy, true);
 *
 *   // Transition between expressions
 *   EyeShape transitioning = EyeShape::lerp(
 *       getExpressionShape(Expression::Neutral, true),
 *       getExpressionShape(Expression::Happy, true),
 *       0.5f  // 50% through transition
 *   );
 * @endcode
 *
 * @author Robot Eyes Project
 * @date 2025
 */

#ifndef EXPRESSIONS_H
#define EXPRESSIONS_H

#include "../eyes/eye_shape.h"

//=============================================================================
// Expression Enumeration
//=============================================================================

/**
 * @enum Expression
 * @brief Available emotional expressions
 *
 * Each expression has a corresponding preset function in the ExpressionPresets
 * namespace. Use getExpressionShape() to retrieve the EyeShape for any expression.
 */
enum class Expression {
    Neutral,        ///< Default relaxed state - standard eye shape
    Happy,          ///< Content, slightly squished with raised outer corners
    Sad,            ///< Droopy outer corners, slightly closed
    Surprised,      ///< Wide open, round
    Angry,          ///< Inner corners raised (angry brow), squinted
    Suspicious,     ///< One eye narrower than the other, skeptical look
    Sleepy,         ///< Heavy lids, half closed, looking slightly down
    Scared,         ///< Wide open, looking down (away from threat)
    Content,        ///< Half-moon smile eyes (basic satisfaction)
    Startled,       ///< Perfect circles, very wide (sudden surprise)
    Grumpy,         ///< Heavy top lid, inner corners up (annoyed)
    Joyful,         ///< Very squished, bouncy (intense happiness)
    Focused,        ///< Slightly narrowed, intense concentration
    Confused,       ///< Asymmetric, tilted (one brow up, one down)
    Yawn,           ///< "> <" tight squeeze with pointed ends
    ContentPetting, ///< Half-closed relaxed eyes for being petted
    Dazed,          ///< Spirals - for being shaken
    Dizzy,          ///< Stars - for being knocked
    Love,           ///< Hearts - for affection after petting
    Joy,            ///< Eyes shut tight, bouncing with happiness

    // Idle micro-expressions
    Curious,        ///< One eye wider, interested look (asymmetric)
    Thinking,       ///< Looking up, slight squint, pondering
    Mischievous,    ///< Sly narrowed eyes, raised outer corner
    Bored,          ///< Heavy lids, looking slightly down
    Alert,          ///< Sudden widening, attentive

    // New expressions using curve/stretch parameters
    Smug,           ///< Curved top edge, sly satisfaction
    Dreamy,         ///< Soft curves, relaxed and wistful
    Skeptical,      ///< Horizontally narrow, one eyebrow raised
    Squint,         ///< Both lids + stretch for intense squint
    Wink,           ///< One eye closed, playful (asymmetric)

    // Breathing exercise
    BreathingPrompt, ///< Alert eyes for breathing reminder
    Relaxed,         ///< Calm, peaceful eyes after breathing exercise

    // Voice assistant
    Listening,       ///< Attentive, ready to hear (wide open, focused)

    COUNT           ///< Number of expressions (for iteration)
};

//=============================================================================
// Expression Access Functions
//=============================================================================

/**
 * @brief Get the EyeShape preset for an expression
 * @param expr The expression to retrieve
 * @param isLeftEye True for left eye (matters for asymmetric expressions)
 * @return Configured EyeShape for the expression
 */
EyeShape getExpressionShape(Expression expr, bool isLeftEye = true);

/**
 * @brief Get display name for debugging/logging
 * @param expr The expression
 * @return Human-readable name string
 */
const char* getExpressionName(Expression expr);

//=============================================================================
// Expression Preset Namespace
//=============================================================================

/**
 * @namespace ExpressionPresets
 * @brief Factory functions for each expression's EyeShape configuration
 *
 * Each function returns a fully configured EyeShape for the named expression.
 * These are the "source of truth" for how each emotion appears visually.
 */
namespace ExpressionPresets {

/**
 * @brief Neutral - Default relaxed state
 *
 * Standard eye shape with no modifications. All parameters at default values.
 * This is the baseline that other expressions deviate from.
 */
inline EyeShape neutral() {
    EyeShape s;
    // All defaults: width=1, height=1, no lid closure, no corner offsets
    return s;
}

/**
 * @brief Happy - Content, friendly appearance
 *
 * Slightly squished vertically with raised outer corners creating a gentle
 * smile shape. Light top lid closure adds warmth.
 */
inline EyeShape happy() {
    EyeShape s;
    s.height = 0.8f;          // Slightly squished vertically
    s.topLid = 0.25f;         // Slight lid closure for warmth
    s.outerCornerY = 0.2f;    // Raised outer corners (smile)
    return s;
}

/**
 * @brief Sad - Melancholy, dejected appearance
 *
 * Droopy outer corners are the key feature. Combined with slightly raised
 * inner corners and partial closure for a downcast look.
 */
inline EyeShape sad() {
    EyeShape s;
    s.height = 0.75f;
    s.topLid = 0.15f;
    s.outerCornerY = -0.3f;   // Droopy outer corners (key sad indicator)
    s.innerCornerY = 0.1f;    // Slightly raised inner corners
    return s;
}

/**
 * @brief Surprised - Sudden astonishment
 *
 * Wide open eyes with increased roundness. Larger than normal in both
 * dimensions to convey the "eyes widening" effect.
 */
inline EyeShape surprised() {
    EyeShape s;
    s.width = 1.2f;           // Wider than normal
    s.height = 1.3f;          // Very tall
    s.cornerRadius = 1.2f;    // More rounded for softer appearance
    return s;
}

/**
 * @brief Angry - Irritated, hostile appearance
 *
 * The classic angry look with raised inner corners (angry brow effect),
 * lowered outer corners, and heavy top lid creating an intense stare.
 */
inline EyeShape angry() {
    EyeShape s;
    s.height = 0.85f;
    s.topLid = 0.3f;          // Heavy brow effect
    s.innerCornerY = 0.35f;   // Raised inner corners (angry brow)
    s.outerCornerY = -0.15f;  // Lowered outer corners
    return s;
}

/**
 * @brief Suspicious - Skeptical, one eye narrower with sideways glance
 *
 * Asymmetric expression where one eye is more squinted than the other.
 * The isLeftEye parameter determines which eye is the squinted one.
 * Uses bottomLid for stronger squint and offsetX for sideways glance.
 *
 * @param isLeftEye If true, left eye is less squinted
 */
inline EyeShape suspicious(bool isLeftEye) {
    EyeShape s;
    s.height = 0.6f;
    s.topLid = 0.35f;
    s.bottomLid = 0.15f;       // Added squint from below
    s.offsetY = 0.15f;         // Slight sideways glance

    // Asymmetric: right eye more squinted (skeptical look)
    if (!isLeftEye) {
        s.height = 0.5f;
        s.topLid = 0.45f;
        s.bottomLid = 0.2f;    // Even more squinted
    }
    return s;
}

/**
 * @brief Sleepy - Drowsy, tired appearance
 *
 * Heavy lids with the eyes looking slightly downward. Both top and bottom
 * lids are partially closed, with top lid being dominant.
 */
inline EyeShape sleepy() {
    EyeShape s;
    s.height = 0.7f;
    s.topLid = 0.5f;          // Very heavy lids (main sleepy indicator)
    s.bottomLid = 0.1f;       // Slight bottom lid closure
    s.offsetY = 0.1f;         // Looking slightly down
    return s;
}

/**
 * @brief Scared - Fearful, wide-eyed looking away
 *
 * Very wide open eyes (like surprised) but with gaze directed downward
 * as if looking away from a threat above.
 */
inline EyeShape scared() {
    EyeShape s;
    s.width = 1.1f;
    s.height = 1.35f;         // Very wide open
    s.offsetY = 0.3f;         // Looking down (away from threat)
    return s;
}

/**
 * @brief Content - Anime-style happy slit eyes "^_^"
 *
 * Thin horizontal slits with rounded ends, like classic anime happy eyes.
 * Simple symmetric pill shape that bounces up and down.
 */
inline EyeShape content() {
    EyeShape s;
    s.width = 0.1f;           // ~12px tall slit on screen
    s.height = 0.8f;          // Wide horizontally (~80px)
    s.cornerRadius = 1.5f;    // Very rounded for pill/slit shape
    return s;
}

/**
 * @brief Startled - Sudden shock, perfect circles
 *
 * Even more extreme than surprised. Eyes become nearly circular,
 * conveying a sudden jolt of surprise or alarm.
 */
inline EyeShape startled() {
    EyeShape s;
    s.width = 1.3f;           // Wider than surprised
    s.height = 1.3f;          // Perfect square aspect (becomes circular)
    s.cornerRadius = 2.0f;    // Very round (approaching circular)
    return s;
}

/**
 * @brief Grumpy - Annoyed, displeased
 *
 * Similar to angry but less intense. Heavy brow look with slight
 * upward gaze as if looking at something with disdain.
 */
inline EyeShape grumpy() {
    EyeShape s;
    s.height = 0.8f;
    s.topLid = 0.45f;         // Heavy brow
    s.innerCornerY = 0.25f;   // Angry inner corners
    s.offsetY = -0.1f;        // Looking slightly up
    return s;
}

/**
 * @brief Joyful - Intense happiness, celebration
 *
 * More extreme than happy. Very squished with strong smile corners
 * and extra squash parameter for bouncy animation feel.
 */
inline EyeShape joyful() {
    EyeShape s;
    s.height = 0.45f;         // Very squished
    s.topLid = 0.45f;
    s.outerCornerY = 0.35f;   // Big smile raise
    s.squash = 0.9f;          // Extra squash for bouncy feel
    return s;
}

/**
 * @brief Focused - Intense concentration
 *
 * Slightly narrowed eyes with stronger squint. Conveys attention
 * and concentration. Uses both lids for intensity.
 */
inline EyeShape focused() {
    EyeShape s;
    s.height = 0.85f;
    s.topLid = 0.25f;          // Slightly more closed
    s.bottomLid = 0.15f;       // More bottom lid for squint
    s.stretch = 0.95f;         // Slight horizontal narrow
    return s;
}

/**
 * @brief Confused - Puzzled, one eyebrow raised
 *
 * Asymmetric expression with tilted corners. One side has raised inner
 * corner, the other has raised outer corner, creating a quizzical look.
 *
 * @param isLeftEye Determines which direction the tilt goes
 */
inline EyeShape confused(bool isLeftEye) {
    EyeShape s;
    s.height = 0.9f;

    // Asymmetric tilt: creates "one eyebrow up" look
    if (isLeftEye) {
        s.innerCornerY = 0.15f;
        s.outerCornerY = -0.1f;
    } else {
        s.innerCornerY = -0.1f;
        s.outerCornerY = 0.15f;
    }
    return s;
}

/**
 * @brief Yawn - Tight squeeze "> <" shape
 *
 * Creates the characteristic yawning squeeze shape using pinch parameters
 * to create pointed tips. Very small and tight.
 */
inline EyeShape yawn() {
    EyeShape s;
    s.width = 0.25f;           // Very tight horizontal squeeze
    s.height = 0.35f;          // Small
    s.cornerRadius = 0.2f;     // Minimal rounding
    s.topPinch = 0.9f;         // Pointed top ("> <" shape)
    s.bottomPinch = 0.9f;      // Pointed bottom
    return s;
}

/**
 * @brief ContentPetting - Anime-style happy slit eyes "^_^"
 *
 * Thin horizontal slits with rounded ends, like classic anime happy eyes.
 * Same as Content/Joy for consistent happy appearance when petted.
 */
inline EyeShape contentPetting() {
    EyeShape s;
    s.width = 0.1f;           // ~12px tall slit on screen
    s.height = 0.8f;          // Wide horizontally (~80px)
    s.cornerRadius = 1.5f;    // Very rounded for pill/slit shape
    return s;
}

/**
 * @brief Dazed - Spirals for shaken expressions
 *
 * Displays rotating spirals when the robot is shaken.
 * The animPhase parameter controls rotation.
 */
inline EyeShape dazed() {
    EyeShape s;
    s.shapeType = ShapeType::Swirl;
    s.height = 1.2f;           // Larger than normal
    s.animPhase = 0.0f;        // Will be animated in main loop
    return s;
}

/**
 * @brief Dizzy - Stars for knocked expressions
 *
 * Displays stars when the robot is knocked (single hard impact).
 * The animPhase parameter controls rotation.
 */
inline EyeShape dizzy() {
    EyeShape s;
    s.shapeType = ShapeType::Star;
    s.starPoints = 5;          // 5-pointed star
    s.height = 1.2f;           // Larger than normal
    s.animPhase = 0.0f;        // Will be animated in main loop
    return s;
}

/**
 * @brief Love - Hearts for affection expressions
 *
 * Displays hearts when showing affection (after being petted).
 * Size can be animated for pulsing effect.
 */
inline EyeShape love() {
    EyeShape s;
    s.shapeType = ShapeType::Heart;
    s.height = 1.0f;           // Normal size, will pulse
    return s;
}

/**
 * @brief Joy - Anime-style happy slit eyes "^_^" with bounce
 *
 * Thin horizontal slits with rounded ends, identical to Content.
 * Simple symmetric pill shape that bounces up and down.
 */
inline EyeShape joy() {
    EyeShape s;
    s.width = 0.1f;           // ~12px tall slit on screen
    s.height = 0.8f;          // Wide horizontally (~80px)
    s.cornerRadius = 1.5f;    // Very rounded for pill/slit shape
    return s;
}

//=============================================================================
// Idle Micro-Expressions
//=============================================================================

/**
 * @brief Curious - One eye wider, interested look
 *
 * Asymmetric expression where one eye opens wider as if noticing
 * something interesting. Creates an inquisitive appearance.
 *
 * @param isLeftEye Determines which eye is wider
 */
inline EyeShape curious(bool isLeftEye) {
    EyeShape s;
    if (isLeftEye) {
        s.height = 1.15f;          // Left eye wider
        s.topLid = 0.0f;
        s.innerCornerY = 0.1f;     // Slight tilt
    } else {
        s.height = 0.9f;           // Right eye slightly smaller
        s.topLid = 0.15f;
        s.innerCornerY = -0.05f;
    }
    return s;
}

/**
 * @brief Thinking - Looking up, slight squint
 *
 * Eyes look upward and slightly to the side with mild squinting,
 * as if pondering something. Conveys thoughtfulness.
 */
inline EyeShape thinking() {
    EyeShape s;
    s.height = 0.9f;
    s.topLid = 0.15f;
    s.offsetX = -0.25f;        // Looking up (after rotation)
    s.offsetY = 0.15f;         // Looking slightly to side
    return s;
}

/**
 * @brief Mischievous - Sly, scheming look
 *
 * Narrowed eyes with raised outer corners creating a sly smirk.
 * Suggests playful mischief or a secret.
 */
inline EyeShape mischievous() {
    EyeShape s;
    s.height = 0.7f;
    s.topLid = 0.3f;
    s.outerCornerY = 0.25f;    // Raised outer (smirk)
    s.innerCornerY = 0.1f;     // Slight inner raise adds cunning
    return s;
}

/**
 * @brief Bored - Heavy lids, disinterested
 *
 * Heavy eyelids with gaze slightly downward and to the side.
 * Conveys disinterest or tedium.
 */
inline EyeShape bored() {
    EyeShape s;
    s.height = 0.75f;
    s.topLid = 0.35f;
    s.offsetY = 0.2f;          // Looking slightly down/aside
    s.outerCornerY = -0.1f;    // Slight droop
    return s;
}

/**
 * @brief Alert - Sudden widening, attentive
 *
 * Eyes suddenly widen as if hearing or noticing something.
 * Less extreme than startled, more like perking up.
 */
inline EyeShape alert() {
    EyeShape s;
    s.width = 1.1f;
    s.height = 1.15f;          // Noticeably wider
    s.cornerRadius = 1.1f;     // Slightly rounder
    return s;
}

//=============================================================================
// New Expressions Using Curve/Stretch Parameters
//=============================================================================

/**
 * @brief Smug - Self-satisfied, curved top edge
 *
 * Uses topCurve to create a sly, self-satisfied appearance.
 * Like knowing a secret or being pleased with oneself.
 */
inline EyeShape smug() {
    EyeShape s;
    s.height = 0.75f;
    s.topLid = 0.25f;
    s.topCurve = 0.5f;         // Curved top edge - key smug feature
    s.outerCornerY = 0.2f;     // Slight smile
    s.innerCornerY = 0.1f;     // Slight inner raise
    return s;
}

/**
 * @brief Dreamy - Soft, wistful, relaxed
 *
 * Uses both curves for a soft, faraway look. Perfect for
 * daydreaming or gentle contentment.
 */
inline EyeShape dreamy() {
    EyeShape s;
    s.height = 0.6f;
    s.topLid = 0.3f;
    s.topCurve = 0.4f;         // Soft curved top
    s.bottomCurve = 0.2f;      // Gentle bottom curve
    s.outerCornerY = 0.15f;    // Gentle smile
    s.offsetX = -0.1f;         // Slight upward gaze
    return s;
}

/**
 * @brief Skeptical - Horizontally narrow, doubting
 *
 * Uses stretch to narrow the eyes horizontally without closing.
 * Asymmetric with one eyebrow raised for the classic skeptical look.
 *
 * @param isLeftEye Determines which eye has the raised brow
 */
inline EyeShape skeptical(bool isLeftEye) {
    EyeShape s;
    s.height = 0.85f;
    s.stretch = 0.8f;          // Horizontally narrow - key skeptical feature
    s.topLid = isLeftEye ? 0.1f : 0.3f;     // Asymmetric lids
    s.innerCornerY = isLeftEye ? 0.2f : 0.0f;  // One brow raised
    s.bottomLid = 0.1f;
    return s;
}

/**
 * @brief Squint - Intense narrowing, both dimensions
 *
 * Uses stretch combined with both lids for an intense squint.
 * Good for bright light reaction or trying to see something far away.
 */
inline EyeShape squint() {
    EyeShape s;
    s.height = 0.7f;
    s.stretch = 0.85f;         // Horizontal narrow
    s.topLid = 0.35f;          // Strong top closure
    s.bottomLid = 0.25f;       // Strong bottom closure
    s.innerCornerY = 0.1f;     // Slight concentration furrow
    return s;
}

/**
 * @brief Wink - Playful one eye closed
 *
 * Strongly asymmetric expression. One eye stays open and alert,
 * the other closes in a playful wink. Great for acknowledgment or mischief.
 *
 * @param isLeftEye Determines which eye winks (right eye winks by default)
 */
inline EyeShape wink(bool isLeftEye) {
    EyeShape s;
    if (isLeftEye) {
        // Left eye stays open with slight smile
        s.height = 1.05f;
        s.topLid = 0.0f;
        s.outerCornerY = 0.1f;     // Slight smile
    } else {
        // Right eye winks - horizontal slit like ^_^ anime eyes
        s.width = 0.15f;           // Thin vertical (becomes horizontal slit)
        s.height = 0.75f;          // Wide horizontal
        s.cornerRadius = 1.5f;     // Rounded ends for pill shape
        s.outerCornerY = 0.15f;    // Slight upward tilt for happy wink
    }
    return s;
}

/**
 * @brief BreathingPrompt - Alert eyes for breathing reminder
 *
 * Slightly larger and rounder eyes to draw attention when
 * the breathing exercise prompt appears.
 */
inline EyeShape breathingPrompt() {
    EyeShape s;
    s.width = 1.1f;            // Slightly larger
    s.height = 1.1f;           // Slightly larger
    s.cornerRadius = 1.2f;     // Rounder for soft appearance
    return s;
}

/**
 * @brief Relaxed - Calm, peaceful eyes after breathing
 *
 * Half-closed with soft curves, conveying deep relaxation
 * and inner peace. Perfect for post-breathing state.
 */
inline EyeShape relaxed() {
    EyeShape s;
    s.height = 0.65f;          // Slightly closed
    s.topLid = 0.25f;          // Gentle drooping top lid
    s.topCurve = 0.3f;         // Soft curved top
    s.bottomCurve = 0.15f;     // Gentle bottom curve
    s.outerCornerY = 0.1f;     // Slight peaceful upturn
    return s;
}

/**
 * @brief Listening - Attentive, ready to hear voice input
 *
 * Wide open eyes with slight upward gaze, conveying attentive
 * listening for voice commands. Alert but calm.
 */
inline EyeShape listening() {
    EyeShape s;
    s.width = 1.1f;            // Slightly wider
    s.height = 1.1f;           // Slightly taller
    s.topLid = 0.0f;           // Fully open
    s.cornerRadius = 1.1f;     // Rounder, softer
    s.offsetX = -0.05f;        // Slight upward gaze (attentive)
    return s;
}

} // namespace ExpressionPresets

//=============================================================================
// Implementation
//=============================================================================

/**
 * @brief Maps Expression enum to corresponding preset function
 */
inline EyeShape getExpressionShape(Expression expr, bool isLeftEye) {
    switch (expr) {
        case Expression::Happy:
            return ExpressionPresets::happy();
        case Expression::Sad:
            return ExpressionPresets::sad();
        case Expression::Surprised:
            return ExpressionPresets::surprised();
        case Expression::Angry:
            return ExpressionPresets::angry();
        case Expression::Suspicious:
            return ExpressionPresets::suspicious(isLeftEye);
        case Expression::Sleepy:
            return ExpressionPresets::sleepy();
        case Expression::Scared:
            return ExpressionPresets::scared();
        case Expression::Content:
            return ExpressionPresets::content();
        case Expression::Startled:
            return ExpressionPresets::startled();
        case Expression::Grumpy:
            return ExpressionPresets::grumpy();
        case Expression::Joyful:
            return ExpressionPresets::joyful();
        case Expression::Focused:
            return ExpressionPresets::focused();
        case Expression::Confused:
            return ExpressionPresets::confused(isLeftEye);
        case Expression::Yawn:
            return ExpressionPresets::yawn();
        case Expression::ContentPetting:
            return ExpressionPresets::contentPetting();
        case Expression::Dazed:
            return ExpressionPresets::dazed();
        case Expression::Dizzy:
            return ExpressionPresets::dizzy();
        case Expression::Love:
            return ExpressionPresets::love();
        case Expression::Joy:
            return ExpressionPresets::joy();
        case Expression::Curious:
            return ExpressionPresets::curious(isLeftEye);
        case Expression::Thinking:
            return ExpressionPresets::thinking();
        case Expression::Mischievous:
            return ExpressionPresets::mischievous();
        case Expression::Bored:
            return ExpressionPresets::bored();
        case Expression::Alert:
            return ExpressionPresets::alert();
        case Expression::Smug:
            return ExpressionPresets::smug();
        case Expression::Dreamy:
            return ExpressionPresets::dreamy();
        case Expression::Skeptical:
            return ExpressionPresets::skeptical(isLeftEye);
        case Expression::Squint:
            return ExpressionPresets::squint();
        case Expression::Wink:
            return ExpressionPresets::wink(isLeftEye);
        case Expression::BreathingPrompt:
            return ExpressionPresets::breathingPrompt();
        case Expression::Relaxed:
            return ExpressionPresets::relaxed();
        case Expression::Listening:
            return ExpressionPresets::listening();
        case Expression::Neutral:
        default:
            return ExpressionPresets::neutral();
    }
}

/**
 * @brief Parse expression from emotion string (from LLM responses)
 * @param emotionStr Emotion name string (e.g., "happy", "thinking")
 * @return Corresponding Expression, or Neutral if not recognized
 */
inline Expression parseExpression(const char* emotionStr) {
    if (!emotionStr || strlen(emotionStr) == 0) return Expression::Neutral;

    // Convert to lowercase for case-insensitive matching
    String lower = String(emotionStr);
    lower.toLowerCase();

    // Map common emotion words to expressions
    if (lower == "neutral") return Expression::Neutral;
    if (lower == "happy" || lower == "joy" || lower == "joyful") return Expression::Happy;
    if (lower == "sad" || lower == "unhappy") return Expression::Sad;
    if (lower == "surprised" || lower == "surprise") return Expression::Surprised;
    if (lower == "angry" || lower == "anger") return Expression::Angry;
    if (lower == "suspicious" || lower == "skeptical") return Expression::Suspicious;
    if (lower == "sleepy" || lower == "tired") return Expression::Sleepy;
    if (lower == "scared" || lower == "fear" || lower == "afraid") return Expression::Scared;
    if (lower == "content" || lower == "satisfied") return Expression::Content;
    if (lower == "startled") return Expression::Startled;
    if (lower == "grumpy" || lower == "annoyed") return Expression::Grumpy;
    if (lower == "focused" || lower == "focus" || lower == "concentration") return Expression::Focused;
    if (lower == "confused" || lower == "confusion" || lower == "puzzled") return Expression::Confused;
    if (lower == "curious" || lower == "curiosity" || lower == "interested") return Expression::Curious;
    if (lower == "thinking" || lower == "thoughtful" || lower == "pondering") return Expression::Thinking;
    if (lower == "mischievous" || lower == "playful") return Expression::Mischievous;
    if (lower == "bored" || lower == "boredom") return Expression::Bored;
    if (lower == "alert" || lower == "attentive") return Expression::Alert;
    if (lower == "smug") return Expression::Smug;
    if (lower == "dreamy" || lower == "wistful") return Expression::Dreamy;
    if (lower == "listening") return Expression::Listening;
    if (lower == "excited" || lower == "excitement") return Expression::Joyful;
    if (lower == "relaxed" || lower == "calm") return Expression::Relaxed;
    if (lower == "love" || lower == "loving" || lower == "affection") return Expression::Love;

    return Expression::Neutral;
}

/**
 * @brief Returns human-readable name for expression (for debugging)
 */
inline const char* getExpressionName(Expression expr) {
    switch (expr) {
        case Expression::Neutral:        return "Neutral";
        case Expression::Happy:          return "Happy";
        case Expression::Sad:            return "Sad";
        case Expression::Surprised:      return "Surprised";
        case Expression::Angry:          return "Angry";
        case Expression::Suspicious:     return "Suspicious";
        case Expression::Sleepy:         return "Sleepy";
        case Expression::Scared:         return "Scared";
        case Expression::Content:        return "Content";
        case Expression::Startled:       return "Startled";
        case Expression::Grumpy:         return "Grumpy";
        case Expression::Joyful:         return "Joyful";
        case Expression::Focused:        return "Focused";
        case Expression::Confused:       return "Confused";
        case Expression::Yawn:           return "Yawn";
        case Expression::ContentPetting: return "ContentPetting";
        case Expression::Dazed:          return "Dazed";
        case Expression::Dizzy:          return "Dizzy";
        case Expression::Love:           return "Love";
        case Expression::Joy:            return "Joy";
        case Expression::Curious:        return "Curious";
        case Expression::Thinking:       return "Thinking";
        case Expression::Mischievous:    return "Mischievous";
        case Expression::Bored:          return "Bored";
        case Expression::Alert:          return "Alert";
        case Expression::Smug:           return "Smug";
        case Expression::Dreamy:         return "Dreamy";
        case Expression::Skeptical:      return "Skeptical";
        case Expression::Squint:         return "Squint";
        case Expression::Wink:           return "Wink";
        case Expression::BreathingPrompt: return "BreathingPrompt";
        case Expression::Relaxed:        return "Relaxed";
        case Expression::Listening:      return "Listening";
        default:                         return "Unknown";
    }
}

#endif // EXPRESSIONS_H
