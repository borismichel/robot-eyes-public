/**
 * Mood Drift - Long-lasting emotional states based on context
 *
 * Replaces Neutral as the fallback expression. Transient behaviors
 * (blinks, micro-expressions, IMU reactions) still layer on top.
 *
 * Two layers:
 *   1. Neglect chain (idle drift): Neutral → Bored → Grumpy → Sad over 25 min
 *   2. Context moods (event-triggered): PostPetting=Happy, PostNoise=Skeptical, etc.
 *      Context moods override the neglect chain while active.
 */

#ifndef MOOD_DRIFT_H
#define MOOD_DRIFT_H

#include <Arduino.h>
#include "expressions.h"

class MoodDrift {
public:
    MoodDrift();

    /** Call every frame */
    void update(uint32_t now);

    /** Get current base mood (replaces Neutral as fallback) */
    Expression getBaseMood() const;

    /** Any interaction: touch, voice, IMU */
    void notifyActivity(uint32_t now);

    /** Petting session ended (love hearts done) */
    void notifyPettingEnd(uint32_t now);

    /** TooLoud audio event */
    void notifyLoudNoise(uint32_t now);

    /** Woke from Sleeping/Drowsy state */
    void notifyWakeFromSleep(uint32_t now);

    /** Voice assistant conversation ended */
    void notifyVoiceInteraction(uint32_t now);

private:
    enum class ContextMood {
        None,
        PostPetting,
        PostNoise,
        PostWake,
        PostVoice
    };

    ContextMood activeContext;
    uint32_t contextStartTime;
    uint32_t contextDuration;
    Expression contextExpression;

    uint32_t lastActivityTime;

    // Neglect chain thresholds (ms since last activity)
    static constexpr uint32_t NEGLECT_BORED  = 5UL * 60 * 1000;   // 5 min
    static constexpr uint32_t NEGLECT_SAD    = 30UL * 60 * 1000;  // 30 min
    static constexpr uint32_t NEGLECT_GRUMPY = 60UL * 60 * 1000;  // 60 min

    // Context durations (ms)
    static constexpr uint32_t POST_PETTING_DUR = 3UL * 60 * 1000; // 3 min
    static constexpr uint32_t POST_NOISE_DUR   = 2UL * 60 * 1000; // 2 min
    static constexpr uint32_t POST_WAKE_DUR    = 2UL * 60 * 1000; // 2 min
    static constexpr uint32_t POST_VOICE_DUR   = 2UL * 60 * 1000; // 2 min
};

#endif // MOOD_DRIFT_H
