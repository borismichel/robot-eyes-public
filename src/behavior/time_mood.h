/**
 * @file time_mood.h
 * @brief Time-of-day based mood system
 *
 * Provides mood modifiers based on the current time of day:
 * - Morning (6am-12pm): Energetic, faster blinks
 * - Afternoon (12pm-6pm): Balanced baseline
 * - Evening (6pm-10pm): Relaxed, slower gaze
 * - Night (10pm-6am): Sleepy, heavier lids
 */

#ifndef TIME_MOOD_H
#define TIME_MOOD_H

#include <Arduino.h>

/**
 * Time-of-day mood periods
 */
enum class TimeMood {
    Morning,    // 6:00 - 11:59 - Energetic
    Afternoon,  // 12:00 - 17:59 - Balanced
    Evening,    // 18:00 - 21:59 - Relaxed
    Night       // 22:00 - 5:59 - Sleepy
};

/**
 * Mood modifiers that affect behavior
 */
struct MoodModifiers {
    float blinkRateMultiplier;   // 0.7 (night) to 1.2 (morning)
    float gazeSpeedMultiplier;   // 0.7 (evening) to 1.0 (normal)
    float baseLidOffset;         // 0.0 (day) to 0.15 (night, heavier lids)
    const char* moodName;        // For debugging
};

/**
 * Get the current mood based on hour (0-23)
 */
inline TimeMood getTimeMood(int hour) {
    if (hour >= 6 && hour < 12) {
        return TimeMood::Morning;
    } else if (hour >= 12 && hour < 18) {
        return TimeMood::Afternoon;
    } else if (hour >= 18 && hour < 22) {
        return TimeMood::Evening;
    } else {
        return TimeMood::Night;
    }
}

/**
 * Get mood modifiers for a given mood
 */
inline MoodModifiers getMoodModifiers(TimeMood mood) {
    switch (mood) {
        case TimeMood::Morning:
            return {
                .blinkRateMultiplier = 1.2f,   // Blink more often (alert)
                .gazeSpeedMultiplier = 1.1f,   // Faster gaze movements
                .baseLidOffset = 0.0f,         // Wide awake
                .moodName = "Morning"
            };

        case TimeMood::Afternoon:
            return {
                .blinkRateMultiplier = 1.0f,   // Normal blink rate
                .gazeSpeedMultiplier = 1.0f,   // Normal gaze speed
                .baseLidOffset = 0.0f,         // Normal lids
                .moodName = "Afternoon"
            };

        case TimeMood::Evening:
            return {
                .blinkRateMultiplier = 0.85f,  // Slightly slower blinks
                .gazeSpeedMultiplier = 0.8f,   // Slower, more relaxed gaze
                .baseLidOffset = 0.05f,        // Slightly heavier lids
                .moodName = "Evening"
            };

        case TimeMood::Night:
            return {
                .blinkRateMultiplier = 0.7f,   // Slow, sleepy blinks
                .gazeSpeedMultiplier = 0.6f,   // Very slow gaze
                .baseLidOffset = 0.12f,        // Heavy lids (drowsy look)
                .moodName = "Night"
            };

        default:
            return {
                .blinkRateMultiplier = 1.0f,
                .gazeSpeedMultiplier = 1.0f,
                .baseLidOffset = 0.0f,
                .moodName = "Unknown"
            };
    }
}

#endif // TIME_MOOD_H
