/**
 * Mood Drift Implementation
 */

#include "mood_drift.h"

MoodDrift::MoodDrift()
    : activeContext(ContextMood::None)
    , contextStartTime(0)
    , contextDuration(0)
    , contextExpression(Expression::Neutral)
    , lastActivityTime(0) {
}

void MoodDrift::update(uint32_t now) {
    // Expire active context if duration has passed
    if (activeContext != ContextMood::None) {
        if (now - contextStartTime >= contextDuration) {
            activeContext = ContextMood::None;
            Serial.println("[MoodDrift] Context expired, back to idle drift");
        }
    }
}

Expression MoodDrift::getBaseMood() const {
    // Context overrides take priority over idle drift
    if (activeContext != ContextMood::None) {
        return contextExpression;
    }

    // Idle drift: computed from time since last activity
    uint32_t now = millis();
    uint32_t idleTime = now - lastActivityTime;

    if (idleTime >= NEGLECT_GRUMPY) {
        return Expression::Grumpy;
    } else if (idleTime >= NEGLECT_SAD) {
        return Expression::Sad;
    } else if (idleTime >= NEGLECT_BORED) {
        return Expression::Bored;
    }

    return Expression::Neutral;
}

void MoodDrift::notifyActivity(uint32_t now) {
    lastActivityTime = now;
    // Activity clears negative contexts but not positive ones
    if (activeContext == ContextMood::PostWake) {
        activeContext = ContextMood::None;
        Serial.println("[MoodDrift] Wake grumpy cleared by interaction");
    }
}

void MoodDrift::notifyPettingEnd(uint32_t now) {
    lastActivityTime = now;
    activeContext = ContextMood::PostPetting;
    contextStartTime = now;
    contextDuration = POST_PETTING_DUR;
    contextExpression = Expression::Happy;
    Serial.println("[MoodDrift] Post-petting happy (3 min)");
}

void MoodDrift::notifyLoudNoise(uint32_t now) {
    // Don't override positive contexts with noise
    if (activeContext == ContextMood::PostPetting ||
        activeContext == ContextMood::PostVoice) return;

    activeContext = ContextMood::PostNoise;
    contextStartTime = now;
    contextDuration = POST_NOISE_DUR;
    contextExpression = Expression::Skeptical;
    Serial.println("[MoodDrift] Post-noise skeptical (2 min)");
}

void MoodDrift::notifyWakeFromSleep(uint32_t now) {
    lastActivityTime = now;
    activeContext = ContextMood::PostWake;
    contextStartTime = now;
    contextDuration = POST_WAKE_DUR;
    contextExpression = Expression::Grumpy;
    Serial.println("[MoodDrift] Post-wake grumpy (2 min)");
}

void MoodDrift::notifyVoiceInteraction(uint32_t now) {
    lastActivityTime = now;
    // Don't override petting afterglow
    if (activeContext == ContextMood::PostPetting) return;

    activeContext = ContextMood::PostVoice;
    contextStartTime = now;
    contextDuration = POST_VOICE_DUR;
    contextExpression = Expression::Content;
    Serial.println("[MoodDrift] Post-voice content (2 min)");
}
