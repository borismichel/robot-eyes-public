/**
 * Emotion Engine - State machine for emotional behavior
 */

#ifndef EMOTION_ENGINE_H
#define EMOTION_ENGINE_H

#include <Arduino.h>
#include "emotion_types.h"
#include "../eyes/expressions.h"
#include "../animation/animator.h"
#include "../animation/blink_controller.h"
#include "../animation/look_controller.h"

/**
 * Emotion Engine - manages emotional state and expression transitions
 */
class EmotionEngine {
public:
    EmotionEngine();

    /**
     * Initialize with default emotion configurations
     */
    void init();

    /**
     * Update emotion state and animations (call every frame)
     * @param left Output left eye parameters
     * @param right Output right eye parameters
     * @param gaze_x Output gaze X
     * @param gaze_y Output gaze Y
     */
    void update(EyeParams& left, EyeParams& right, float& gaze_x, float& gaze_y);

    /**
     * Set emotion immediately
     */
    void set_emotion(Emotion emotion);

    /**
     * Transition to emotion with animation
     */
    void transition_to(Emotion emotion);

    /**
     * Trigger an event
     */
    void trigger(TriggerEvent event);

    /**
     * Get current emotion
     */
    Emotion current_emotion() const { return m_current_emotion; }

    /**
     * Set emotion weight (affects auto-selection probability)
     */
    void set_emotion_weight(Emotion emotion, float weight);

    /**
     * Enable/disable auto emotion changes
     */
    void set_auto_emotion(bool enabled);

    /**
     * Access controllers for direct manipulation
     */
    BlinkController& blink() { return m_blink; }
    LookController& look() { return m_look; }
    Animator& animator() { return m_animator; }

private:
    void select_next_emotion();
    Emotion select_emotion_for_trigger(TriggerEvent event);
    Emotion weighted_random_emotion();
    void schedule_next_change();

    // Current state
    Emotion m_current_emotion;
    Expression m_current_expression;
    uint32_t m_emotion_start_time;
    uint32_t m_emotion_duration;

    // Emotion configurations
    EmotionConfig m_configs[static_cast<int>(Emotion::COUNT)];

    // Controllers
    Animator m_animator;
    BlinkController m_blink;
    LookController m_look;

    // Auto-change settings
    bool m_auto_enabled;
    uint32_t m_next_change_time;

    // Previous state (for returning after temporary emotions)
    Emotion m_previous_emotion;
};

#endif // EMOTION_ENGINE_H
