/**
 * Emotion Engine Implementation
 */

#include "emotion_engine.h"

EmotionEngine::EmotionEngine()
    : m_current_emotion(Emotion::NEUTRAL)
    , m_emotion_start_time(0)
    , m_emotion_duration(0)
    , m_auto_enabled(true)
    , m_next_change_time(0)
    , m_previous_emotion(Emotion::NEUTRAL) {
}

void EmotionEngine::init() {
    // Initialize emotion configurations
    for (int i = 0; i < static_cast<int>(Emotion::COUNT); i++) {
        m_configs[i] = EmotionConfig::defaults(static_cast<Emotion>(i));
    }

    // Set initial expression
    m_current_expression = get_expression(Emotion::NEUTRAL);
    m_animator.set_immediate(m_current_expression);

    // Schedule first auto-change
    schedule_next_change();

    Serial.println("Emotion engine initialized");
}

void EmotionEngine::update(EyeParams& left, EyeParams& right, float& gaze_x, float& gaze_y) {
    uint32_t now = millis();

    // Check for auto emotion change
    if (m_auto_enabled && now >= m_next_change_time) {
        select_next_emotion();
    }

    // Update animator
    m_animator.update();

    // Get current animated expression
    left = m_animator.current_left();
    right = m_animator.current_right();

    // Apply blink
    m_blink.update(left, right);

    // Update look controller
    m_look.update(gaze_x, gaze_y);
}

void EmotionEngine::set_emotion(Emotion emotion) {
    m_current_emotion = emotion;
    m_current_expression = get_expression(emotion);
    m_animator.set_immediate(m_current_expression);
    m_emotion_start_time = millis();

    const EmotionConfig& cfg = m_configs[static_cast<int>(emotion)];
    m_emotion_duration = random(cfg.min_duration, cfg.max_duration);

    schedule_next_change();
}

void EmotionEngine::transition_to(Emotion emotion) {
    m_previous_emotion = m_current_emotion;
    m_current_emotion = emotion;
    m_current_expression = get_expression(emotion);

    const EmotionConfig& cfg = m_configs[static_cast<int>(emotion)];
    m_animator.animate_to(m_current_expression, cfg.transition_time, EaseType::EASE_IN_OUT);

    m_emotion_start_time = millis();
    m_emotion_duration = random(cfg.min_duration, cfg.max_duration);

    schedule_next_change();
}

void EmotionEngine::trigger(TriggerEvent event) {
    Emotion response = select_emotion_for_trigger(event);

    if (response != m_current_emotion) {
        // Special handling for some events
        if (event == TriggerEvent::TAP) {
            m_blink.blink();  // Blink on tap
        }

        transition_to(response);
    }
}

Emotion EmotionEngine::select_emotion_for_trigger(TriggerEvent event) {
    switch (event) {
        case TriggerEvent::TAP:
            return Emotion::SURPRISED;

        case TriggerEvent::DOUBLE_TAP:
            return Emotion::HAPPY;

        case TriggerEvent::LONG_PRESS:
            if (random(100) < 50) {
                return Emotion::ANNOYED;
            } else {
                return Emotion::SLEEPY;
            }

        case TriggerEvent::SWIPE_LEFT:
        case TriggerEvent::SWIPE_RIGHT:
            return Emotion::CONFUSED;

        case TriggerEvent::SWIPE_UP:
            return Emotion::EXCITED;

        case TriggerEvent::SWIPE_DOWN:
            return Emotion::SAD;

        case TriggerEvent::SHAKE:
            return Emotion::DIZZY;

        case TriggerEvent::TILT_LEFT:
        case TriggerEvent::TILT_RIGHT:
            return Emotion::SUSPICIOUS;

        case TriggerEvent::FLIP:
            return Emotion::SCARED;

        case TriggerEvent::IDLE_LONG:
            return Emotion::SLEEPY;

        case TriggerEvent::WAKE:
            return Emotion::SURPRISED;

        default:
            return m_current_emotion;
    }
}

void EmotionEngine::select_next_emotion() {
    Emotion next = weighted_random_emotion();

    // Avoid selecting the same emotion twice in a row (unless high weight)
    int attempts = 0;
    while (next == m_current_emotion && attempts < 3) {
        next = weighted_random_emotion();
        attempts++;
    }

    transition_to(next);
}

Emotion EmotionEngine::weighted_random_emotion() {
    // Calculate total weight of selectable emotions
    float total_weight = 0.0f;
    for (int i = 0; i < static_cast<int>(Emotion::COUNT); i++) {
        if (m_configs[i].can_auto_select) {
            total_weight += m_configs[i].weight;
        }
    }

    // Random selection with weights
    float target = (float)random(1000) / 1000.0f * total_weight;
    float cumulative = 0.0f;

    for (int i = 0; i < static_cast<int>(Emotion::COUNT); i++) {
        if (m_configs[i].can_auto_select) {
            cumulative += m_configs[i].weight;
            if (target <= cumulative) {
                return static_cast<Emotion>(i);
            }
        }
    }

    return Emotion::NEUTRAL;
}

void EmotionEngine::schedule_next_change() {
    m_next_change_time = millis() + m_emotion_duration;
}

void EmotionEngine::set_emotion_weight(Emotion emotion, float weight) {
    int idx = static_cast<int>(emotion);
    if (idx >= 0 && idx < static_cast<int>(Emotion::COUNT)) {
        m_configs[idx].weight = max(0.0f, weight);
    }
}

void EmotionEngine::set_auto_emotion(bool enabled) {
    m_auto_enabled = enabled;
    if (enabled) {
        schedule_next_change();
    }
}
