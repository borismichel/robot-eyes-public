/**
 * @file breathing_exercise.h
 * @brief Mindfulness breathing exercises with scheduled reminders
 *
 * Two exercise types:
 * - Box breathing: 5-5-5-5 (inhale, hold, exhale, hold) × 3 = 60s
 * - Nadi Shodhana: alternate nostril, 4s phases × 6 × 3 = 72s
 *
 * Scheduled reminders during configurable time windows
 */

#ifndef BREATHING_EXERCISE_H
#define BREATHING_EXERCISE_H

#include <Arduino.h>
#include <Preferences.h>
#include "../eyes/eye_shape.h"
#include "../ui/text_renderer.h"

// Box breathing timings
#define BREATHING_PHASE_MS      5000    // 5 seconds per phase
#define BREATHING_CYCLES        3       // Number of cycles (4 phases × 3 = 60s)

// Nadi Shodhana timings
#define NADI_PHASE_MS           4000    // 4 seconds per phase
#define NADI_CYCLES             3       // Number of cycles (6 phases × 3 = 72s)

// Shared timings
#define BREATHING_PROMPT_TIMEOUT_MS  30000  // 30 seconds to tap Start/Skip
#define BREATHING_CONFIRM_MS    2500    // 2.5 seconds for "Let's Breathe" confirmation
#define BREATHING_COMPLETE_MS   2000    // 2 seconds at completion

// Default settings
#define DEFAULT_BREATHING_START_HOUR  9
#define DEFAULT_BREATHING_END_HOUR    17
#define DEFAULT_BREATHING_INTERVAL    60  // minutes

enum class BreathingType {
    BoxBreathing,       // 5-5-5-5, symmetric eyes
    NadiShodhana        // Alternate nostril, asymmetric eyes
};

enum class BreathingState {
    Disabled,       // Feature turned off
    Idle,           // Monitoring schedule, waiting for trigger
    ShowingPrompt,  // "BREATHE" prompt with Start/Skip buttons
    Confirmation,   // "Let's Breathe" confirmation (2.5s fade to inhale)
    Inhale,         // Breathing in (5s) - eyes inflate
    HoldIn,         // Holding breath (5s) - eyes stay large
    Exhale,         // Breathing out (5s) - eyes deflate
    HoldOut,        // Holding empty (5s) - eyes nearly closed
    Complete        // Done, returning to normal (2s)
};

/**
 * @class BreathingExercise
 * @brief Manages mindfulness breathing exercises with scheduling
 */
class BreathingExercise {
public:
    BreathingExercise();

    /**
     * @brief Initialize and load settings
     */
    void begin();

    /**
     * @brief Update state machine (call every frame)
     * @param dt Delta time in seconds
     * @param currentHour Current hour (0-23)
     * @param currentMinute Current minute (0-59)
     * @return true if state changed (for sound triggers)
     */
    bool update(float dt, int currentHour, int currentMinute);

    // User actions
    void start();       // Start breathing from prompt screen
    void skip();        // Skip/dismiss prompt (reschedule)
    void triggerNow();  // Manual trigger (doesn't affect scheduled timing)

    /**
     * @brief Set whether pomodoro is currently active
     * Breathing reminders won't trigger during active pomodoro
     */
    void setPomodoroActive(bool active) { pomodoroActive = active; }

    // State queries
    BreathingState getState() const { return state; }
    bool isActive() const;        // In any breathing phase
    bool isShowingPrompt() const { return state == BreathingState::ShowingPrompt; }
    bool isInConfirmation() const { return state == BreathingState::Confirmation; }
    bool needsFullScreenRender() const;  // True when breathing takes over display

    /**
     * @brief Check if there's a pending state change from external action
     * Call this after update() to catch state changes from triggerNow()/start()/skip()
     * @return true if state changed externally (clears the flag)
     */
    bool consumeExternalStateChange();

    /**
     * @brief Get current eye shape for breathing animation (both eyes same)
     * @param out Output eye shape
     */
    void getTargetShape(EyeShape& out) const;

    /**
     * @brief Get separate left/right eye shapes (for Nadi Shodhana asymmetry)
     */
    void getTargetShapes(EyeShape& leftOut, EyeShape& rightOut) const;

    /**
     * @brief Get progress within current phase (0.0-1.0)
     */
    float getPhaseProgress() const;

    /**
     * @brief Render the prompt screen to buffer
     * @param buffer Pixel buffer
     * @param bufW Buffer width
     * @param bufH Buffer height
     * @param eyeColor Eye color for accent
     */
    void renderPromptScreen(uint16_t* buffer, int16_t bufW, int16_t bufH, uint16_t eyeColor);

    /**
     * @brief Render the "Let's Breathe" confirmation screen
     * @param buffer Pixel buffer
     * @param bufW Buffer width
     * @param bufH Buffer height
     * @param eyeColor Eye color for accent
     */
    void renderConfirmationScreen(uint16_t* buffer, int16_t bufW, int16_t bufH, uint16_t eyeColor);

    /**
     * @brief Render phase text overlay ("IN", "HOLD", "OUT") below eyes
     * @param buffer Pixel buffer
     * @param bufW Buffer width
     * @param bufH Buffer height
     * @param eyeColor Eye color for text
     */
    void renderPhaseText(uint16_t* buffer, int16_t bufW, int16_t bufH, uint16_t eyeColor);

    /**
     * @brief Render phase text as overlay (no buffer clear) for Nadi Shodhana
     * Draws text at bottom third of screen, on top of rendered eyes
     */
    void renderPhaseTextOverlay(uint16_t* buffer, int16_t bufW, int16_t bufH, uint16_t eyeColor);

    /**
     * @brief Get pulse alpha for "BREATHE" text animation (0.0-1.0)
     */
    float getPulseAlpha() const;

    // Settings getters
    bool isEnabled() const { return enabled; }
    bool isSoundEnabled() const { return soundEnabled; }
    int getStartHour() const { return startHour; }
    int getEndHour() const { return endHour; }
    int getIntervalMinutes() const { return intervalMinutes; }
    BreathingType getExerciseType() const { return exerciseType; }

    // Settings setters
    void setEnabled(bool en);
    void setSoundEnabled(bool en);
    void setTimeWindow(int start, int end);
    void setIntervalMinutes(int minutes);
    void setExerciseType(BreathingType type);

private:
    BreathingState state;
    uint32_t stateStartTime;        // When current state started (millis)
    uint32_t lastTriggerTime;       // Last time we triggered a reminder
    int currentCycle;               // Current cycle (0-2)
    BreathingState previousState;   // For detecting transitions
    bool pomodoroActive;            // Block scheduled triggers during pomodoro
    bool externalStateChange;       // Flag for state changes from triggerNow/start/skip
    bool nadiSecondHalf;            // Nadi Shodhana: true = right-to-left half of cycle

    // Settings (persisted)
    bool enabled;
    bool soundEnabled;  // Play sounds during breathing (default: true)
    int startHour;      // 0-23
    int endHour;        // 0-23
    int intervalMinutes; // 30-180
    BreathingType exerciseType;

    Preferences prefs;

    void loadSettings();
    void saveSettings();
    void setState(BreathingState newState);
    bool shouldTrigger(int hour, int minute);
    uint32_t getStateDuration() const;
};

#endif // BREATHING_EXERCISE_H
