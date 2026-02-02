/**
 * Emotion Types and Configuration
 */

#ifndef EMOTION_TYPES_H
#define EMOTION_TYPES_H

#include "../eyes/expressions.h"

/**
 * Configuration for an emotion's behavior
 */
struct EmotionConfig {
    Emotion emotion;
    float weight;           // Selection weight (higher = more likely)
    uint32_t min_duration;  // Minimum time to stay in this emotion (ms)
    uint32_t max_duration;  // Maximum time to stay in this emotion (ms)
    uint32_t transition_time; // Time to transition to this emotion (ms)
    bool can_auto_select;   // Can be auto-selected during idle

    static EmotionConfig defaults(Emotion e) {
        EmotionConfig config;
        config.emotion = e;
        config.transition_time = 300;
        config.can_auto_select = true;

        switch (e) {
            case Emotion::NEUTRAL:
                config.weight = 5.0f;
                config.min_duration = 3000;
                config.max_duration = 8000;
                break;

            case Emotion::HAPPY:
                config.weight = 2.0f;
                config.min_duration = 2000;
                config.max_duration = 5000;
                break;

            case Emotion::SAD:
                config.weight = 0.5f;
                config.min_duration = 3000;
                config.max_duration = 6000;
                break;

            case Emotion::SURPRISED:
                config.weight = 1.0f;
                config.min_duration = 500;
                config.max_duration = 2000;
                config.transition_time = 100;  // Quick surprise
                break;

            case Emotion::ANGRY:
                config.weight = 0.3f;
                config.min_duration = 2000;
                config.max_duration = 4000;
                break;

            case Emotion::SUSPICIOUS:
                config.weight = 1.5f;
                config.min_duration = 1500;
                config.max_duration = 4000;
                break;

            case Emotion::TIRED:
                config.weight = 1.0f;
                config.min_duration = 3000;
                config.max_duration = 7000;
                break;

            case Emotion::EXCITED:
                config.weight = 1.5f;
                config.min_duration = 1000;
                config.max_duration = 3000;
                config.transition_time = 150;
                break;

            case Emotion::CONFUSED:
                config.weight = 1.0f;
                config.min_duration = 1500;
                config.max_duration = 3500;
                break;

            case Emotion::FOCUSED:
                config.weight = 2.0f;
                config.min_duration = 2000;
                config.max_duration = 5000;
                break;

            case Emotion::SHY:
                config.weight = 0.8f;
                config.min_duration = 2000;
                config.max_duration = 4000;
                break;

            case Emotion::LOVE:
                config.weight = 0.3f;
                config.min_duration = 2000;
                config.max_duration = 4000;
                config.can_auto_select = false;  // Only triggered by events
                break;

            case Emotion::DIZZY:
                config.weight = 0.0f;
                config.min_duration = 1500;
                config.max_duration = 3000;
                config.can_auto_select = false;  // Only triggered by shake
                break;

            case Emotion::ANNOYED:
                config.weight = 0.5f;
                config.min_duration = 2000;
                config.max_duration = 4000;
                break;

            case Emotion::SCARED:
                config.weight = 0.2f;
                config.min_duration = 1000;
                config.max_duration = 3000;
                config.transition_time = 80;
                break;

            case Emotion::SLEEPY:
                config.weight = 0.8f;
                config.min_duration = 4000;
                config.max_duration = 8000;
                break;

            default:
                config.weight = 1.0f;
                config.min_duration = 2000;
                config.max_duration = 5000;
                break;
        }

        return config;
    }
};

/**
 * Trigger events that can cause emotion changes
 */
enum class TriggerEvent {
    NONE,
    TAP,            // Screen tap
    DOUBLE_TAP,     // Double tap
    LONG_PRESS,     // Long press
    SWIPE_LEFT,     // Swipe gesture
    SWIPE_RIGHT,
    SWIPE_UP,
    SWIPE_DOWN,
    SHAKE,          // Device shaken
    TILT_LEFT,      // Device tilted
    TILT_RIGHT,
    FLIP,           // Upside down
    IDLE_LONG,      // Idle for a long time
    WAKE,           // Wake from sleep
};

#endif // EMOTION_TYPES_H
