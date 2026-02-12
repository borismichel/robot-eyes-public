/**
 * @file timer_visuals.h
 * @brief Timer expression side-effects and animations
 *
 * Manages pomodoro/countdown expression changes, concentrate animation,
 * tick sounds, and progress bar clear animation state.
 */

#ifndef TIMER_VISUALS_H
#define TIMER_VISUALS_H

#include <Arduino.h>
#include "../behavior/expression_manager.h"
#include "pomodoro.h"
#include "countdown_timer.h"

// Forward declarations
class AudioPlayer;
class ReactiveAnimations;

class TimerVisuals {
public:
    TimerVisuals();
    void begin(ExpressionManager& em, AudioPlayer& audio, ReactiveAnimations& ra);

    /**
     * @brief Update pomodoro expression state, concentrate animation, tick sounds
     * @return true if pomodoro state changed (caller should reset progress bar cache)
     */
    bool updatePomodoro(PomodoroTimer& timer, uint32_t now);

    /**
     * @brief Update countdown expression state, dismiss logic, tick sounds
     * @param shouldDismissExpired true if another behavior should override expired flash
     * @return true if countdown state changed (caller should reset progress bar cache)
     */
    bool updateCountdown(CountdownTimer& timer, uint32_t now, bool shouldDismissExpired);

    // --- Expression token queries ---
    bool hasActiveExpression() const { return pomodoroToken != EXPR_TOKEN_INVALID || countdownToken != EXPR_TOKEN_INVALID; }

    // --- Concentrate animation ---
    bool isConcentrating() const { return concentratePhase > 0; }

    // --- Progress bar clear animation ---
    bool isClearing() const { return progressBarClearing; }

    /**
     * @brief Advance clear animation, return current progress (0.0-1.0)
     * Returns -1.0f if not clearing. Sets progressBarClearing=false when done.
     */
    float advanceClearAnimation(uint32_t now);

private:
    ExpressionManager* pExprMgr;
    AudioPlayer* pAudio;
    ReactiveAnimations* pReactive;

    // Expression tokens
    uint8_t pomodoroToken;
    uint8_t countdownToken;

    // Pomodoro state tracking
    PomodoroState lastPomodoroState;
    uint32_t lastPomodoroTick;

    // Countdown state tracking
    CountdownState lastCountdownState;
    uint32_t lastCountdownTick;

    // Concentrate animation (Sleepy → Alert → Focused)
    int concentratePhase;       // 0=none, 1=eyes closed, 2=eyes wide
    uint32_t concentrateStart;
    static const uint32_t CONCENTRATE_CLOSE_DURATION = 600;
    static const uint32_t CONCENTRATE_ALERT_DURATION = 900;

    // Progress bar clear animation
    bool progressBarClearing;
    uint32_t clearAnimStart;
    float clearAnimProgress;
    static const uint32_t CLEAR_ANIM_DURATION = 500;

    void startClearAnimation(uint32_t now);
};

#endif // TIMER_VISUALS_H
