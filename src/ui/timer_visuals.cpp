/**
 * @file timer_visuals.cpp
 * @brief Timer expression side-effects implementation
 */

#include "timer_visuals.h"
#include "../behavior/reactive_animations.h"
#include "../audio/audio_player.h"

TimerVisuals::TimerVisuals()
    : pExprMgr(nullptr)
    , pAudio(nullptr)
    , pReactive(nullptr)
    , pomodoroToken(EXPR_TOKEN_INVALID)
    , countdownToken(EXPR_TOKEN_INVALID)
    , lastPomodoroState(PomodoroState::Idle)
    , lastPomodoroTick(0)
    , lastCountdownState(CountdownState::Idle)
    , lastCountdownTick(0)
    , concentratePhase(0)
    , concentrateStart(0)
    , progressBarClearing(false)
    , clearAnimStart(0)
    , clearAnimProgress(0.0f)
{
}

void TimerVisuals::begin(ExpressionManager& em, AudioPlayer& audio, ReactiveAnimations& ra) {
    pExprMgr = &em;
    pAudio = &audio;
    pReactive = &ra;
}

bool TimerVisuals::updatePomodoro(PomodoroTimer& timer, uint32_t now) {
    PomodoroState pomodoroState = timer.getState();
    bool stateChanged = false;

    // Handle state transitions
    if (pomodoroState != lastPomodoroState) {
        stateChanged = true;

        if (pomodoroState == PomodoroState::Working) {
            // Starting work session - trigger concentrate animation
            if (pomodoroToken != EXPR_TOKEN_INVALID) {
                pExprMgr->update(pomodoroToken, Expression::Sleepy);
            } else {
                pomodoroToken = pExprMgr->request(Expression::Sleepy, ExprPriority::Timer);
            }
            concentratePhase = 1;
            concentrateStart = now;
            pReactive->cancelJoy();
            Serial.println("Pomodoro: Work starting - Concentrate animation");

        } else if (pomodoroState == PomodoroState::ShortBreak || pomodoroState == PomodoroState::LongBreak) {
            // Starting break - relaxed expression
            if (pomodoroToken != EXPR_TOKEN_INVALID) {
                pExprMgr->update(pomodoroToken, Expression::Content);
            } else {
                pomodoroToken = pExprMgr->request(Expression::Content, ExprPriority::Timer);
            }
            concentratePhase = 0;
            pReactive->cancelJoy();
            pReactive->resetBounce();
            pReactive->scheduleNextJoy(now);
            Serial.println("Pomodoro: Break started - Content expression");

        } else if (pomodoroState == PomodoroState::Celebration) {
            concentratePhase = 0;
            if (lastPomodoroState == PomodoroState::Working) {
                // Work complete - celebrate with Joy!
                if (pomodoroToken != EXPR_TOKEN_INVALID) {
                    pExprMgr->update(pomodoroToken, Expression::Joy);
                } else {
                    pomodoroToken = pExprMgr->request(Expression::Joy, ExprPriority::Timer);
                }
                pReactive->triggerJoy(now);
                Serial.println("Pomodoro: Work complete - Joy celebration with bounce!");
            } else {
                // Break complete - Content expression
                if (pomodoroToken != EXPR_TOKEN_INVALID) {
                    pExprMgr->update(pomodoroToken, Expression::Content);
                } else {
                    pomodoroToken = pExprMgr->request(Expression::Content, ExprPriority::Timer);
                }
                pReactive->cancelJoy();
                pReactive->resetBounce();
                Serial.println("Pomodoro: Break complete - Content expression");
            }

        } else if (pomodoroState == PomodoroState::Idle && pExprMgr->isActive(pomodoroToken)) {
            // Timer stopped - release expression, start clear animation
            pExprMgr->release(pomodoroToken);
            pomodoroToken = EXPR_TOKEN_INVALID;
            concentratePhase = 0;
            pReactive->cancelJoy();
            startClearAnimation(now);
            Serial.println("Pomodoro: Stopped - clearing progress bar");
        }

        lastPomodoroState = pomodoroState;
    }

    // Concentrate animation phases
    if (concentratePhase > 0) {
        uint32_t elapsed = now - concentrateStart;
        if (concentratePhase == 1 && elapsed >= CONCENTRATE_CLOSE_DURATION) {
            // Phase 1 done → Phase 2: Eyes snap open wide
            concentratePhase = 2;
            concentrateStart = now;
            pExprMgr->update(pomodoroToken, Expression::Alert);
            Serial.println("Pomodoro: Concentrate - Eyes wide!");
        } else if (concentratePhase == 2 && elapsed >= CONCENTRATE_ALERT_DURATION) {
            // Phase 2 done → Settle into Focused
            concentratePhase = 0;
            pExprMgr->update(pomodoroToken, Expression::Focused);
            Serial.println("Pomodoro: Concentrate complete - Focused");
        }
    }

    // Tick sound in last 60 seconds
    if (timer.isActive() && timer.isTickingEnabled() && timer.isLastMinute()) {
        uint32_t remaining = timer.getRemainingSeconds();
        if (remaining != (lastPomodoroTick / 1000)) {
            lastPomodoroTick = remaining * 1000;
            if (pAudio && !pAudio->isPlaying()) {
                Serial.printf("Tick: %lu seconds remaining\n", remaining);
                pAudio->play("/tick.mp3");
            } else {
                Serial.printf("Tick skipped (audio busy): %lu seconds\n", remaining);
            }
        }
    }

    return stateChanged;
}

bool TimerVisuals::updateCountdown(CountdownTimer& timer, uint32_t now, bool shouldDismissExpired) {
    CountdownState countdownState = timer.getState();
    bool stateChanged = false;

    // Handle state transitions
    if (countdownState != lastCountdownState) {
        stateChanged = true;

        if (countdownState == CountdownState::Running) {
            // Request token to track active state (keep current expression)
            if (countdownToken == EXPR_TOKEN_INVALID) {
                countdownToken = pExprMgr->request(pExprMgr->getCurrent(), ExprPriority::Timer);
            }

        } else if (countdownState == CountdownState::Celebration) {
            // Timer finished! Happy expression + sound
            if (countdownToken != EXPR_TOKEN_INVALID) {
                pExprMgr->update(countdownToken, Expression::Happy);
            } else {
                countdownToken = pExprMgr->request(Expression::Happy, ExprPriority::Timer);
            }
            pReactive->triggerJoy(now);
            if (pAudio) pAudio->play("/happy.mp3");
            Serial.println("Timer: Celebration!");

        } else if (countdownState == CountdownState::Expired) {
            // Celebration done → pulsing 00:00 flash
            if (pExprMgr->isActive(countdownToken)) {
                pExprMgr->release(countdownToken);
                countdownToken = EXPR_TOKEN_INVALID;
                pReactive->cancelJoy();
            }
            Serial.println("Timer: Expired flash started");

        } else if (countdownState == CountdownState::Idle) {
            // Timer dismissed or stopped
            if (pExprMgr->isActive(countdownToken)) {
                pExprMgr->release(countdownToken);
                countdownToken = EXPR_TOKEN_INVALID;
                pReactive->cancelJoy();
            }
            startClearAnimation(now);
            Serial.println("Timer: Stopped, restoring expression");
        }

        lastCountdownState = countdownState;
    }

    // Dismiss expired flash when other screen-taking behaviors start
    if (countdownState == CountdownState::Expired && shouldDismissExpired) {
        timer.stop();
        startClearAnimation(now);
        Serial.println("Timer: Expired flash overridden by other behavior");
    }

    // Tick sound in last 60 seconds
    if (timer.isActive() && timer.isTickingEnabled() && timer.isLastMinute()) {
        uint32_t remaining = timer.getRemainingSeconds();
        if (remaining != (lastCountdownTick / 1000)) {
            lastCountdownTick = remaining * 1000;
            if (pAudio && !pAudio->isPlaying()) {
                pAudio->play("/tick.mp3");
            }
        }
    }

    return stateChanged;
}

float TimerVisuals::advanceClearAnimation(uint32_t now) {
    if (!progressBarClearing) return -1.0f;

    uint32_t elapsed = now - clearAnimStart;
    clearAnimProgress = (float)elapsed / CLEAR_ANIM_DURATION;

    if (clearAnimProgress >= 1.0f) {
        clearAnimProgress = 1.0f;
        progressBarClearing = false;
    }

    return clearAnimProgress;
}

void TimerVisuals::startClearAnimation(uint32_t now) {
    progressBarClearing = true;
    clearAnimStart = now;
    clearAnimProgress = 0.0f;
}
