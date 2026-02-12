/**
 * @file reactive_animations.cpp
 * @brief Timed reactive expression animations implementation
 */

#include "reactive_animations.h"
#include "mood_drift.h"
#include "../audio/audio_player.h"

ReactiveAnimations::ReactiveAnimations()
    : pExprMgr(nullptr)
    , pMoodDrift(nullptr)
    , pAudioPlayer(nullptr)
    , imuReacting(false)
    , imuReactionStart(0)
    , imuToken(EXPR_TOKEN_INVALID)
    , showingIrritated(false)
    , irritatedStart(0)
    , irritatedToken(EXPR_TOKEN_INVALID)
    , showingLove(false)
    , loveStart(0)
    , loveToken(EXPR_TOKEN_INVALID)
    , showingJoy(false)
    , joyStart(0)
    , nextJoyTime(0)
    , joyToken(EXPR_TOKEN_INVALID)
    , lastOrientation(Orientation::Normal)
    , orientationToken(EXPR_TOKEN_INVALID)
    , breathingCompletePending(false)
    , breathingSoundEnabled(false)
    , breathingCompleteTime(0)
    , breathingContentUntil(0)
    , breathingRelaxedUntil(0)
    , breathingToken(EXPR_TOKEN_INVALID)
    , bouncePhase(0.0f)
    , shapeAnimPhase(0.0f)
{
}

void ReactiveAnimations::begin(ExpressionManager& em, MoodDrift& mood, AudioPlayer& audio) {
    pExprMgr = &em;
    pMoodDrift = &mood;
    pAudioPlayer = &audio;

    // Schedule first joy (10-30 minutes)
    nextJoyTime = millis() + JOY_MIN_INTERVAL + random(JOY_MAX_INTERVAL - JOY_MIN_INTERVAL);
    Serial.printf("First joy scheduled in %lu minutes\n", (nextJoyTime - millis()) / 60000);
}

void ReactiveAnimations::update(float dt, uint32_t now) {
    // Update IMU reaction timer
    if (imuReacting && (now - imuReactionStart > IMU_REACTION_DURATION)) {
        pExprMgr->release(imuToken);
        imuToken = EXPR_TOKEN_INVALID;
        imuReacting = false;
    }

    // Update irritated timer
    if (showingIrritated && (now - irritatedStart >= IRRITATED_DURATION)) {
        pExprMgr->release(irritatedToken);
        irritatedToken = EXPR_TOKEN_INVALID;
        showingIrritated = false;
        Serial.println("Irritated done, returning to base mood");
    }

    // Update love timer
    if (showingLove && (now - loveStart >= LOVE_DURATION)) {
        pMoodDrift->notifyPettingEnd(now);
        pExprMgr->release(loveToken);
        loveToken = EXPR_TOKEN_INVALID;
        showingLove = false;
        Serial.println("Love hearts done - happy afterglow");
    }

    // Update joy animation
    if (showingJoy) {
        bouncePhase += dt * 3.0f;
        if (now - joyStart > JOY_DURATION) {
            showingJoy = false;
            pExprMgr->release(joyToken);
            joyToken = EXPR_TOKEN_INVALID;
            Serial.println("Joy ended");
        }
    }

    // Update shape animation phase (rotating stars, pulsing hearts)
    shapeAnimPhase += dt * 0.5f;
    if (shapeAnimPhase >= 1.0f) shapeAnimPhase -= 1.0f;

    // Breathing post-completion: 1s delay → Content (3s) → Relaxed (60s) → done
    if (breathingCompletePending && now - breathingCompleteTime >= 1000) {
        breathingCompletePending = false;
        breathingToken = pExprMgr->request(Expression::Content, ExprPriority::Behavior);
        bouncePhase = 0.0f;
        breathingContentUntil = now + 3000;
        if (breathingSoundEnabled && pAudioPlayer && !pAudioPlayer->isPlaying()) {
            pAudioPlayer->play("/happy.mp3");
        }
        Serial.println("[Breathing] Complete - showing Content expression for 3s");
    }

    if (breathingContentUntil > 0 && now >= breathingContentUntil) {
        breathingContentUntil = 0;
        pExprMgr->update(breathingToken, Expression::Relaxed);
        breathingRelaxedUntil = now + 60000;
        Serial.println("[Breathing] Transitioning to Relaxed for 60s");
    }

    if (breathingRelaxedUntil > 0 && now >= breathingRelaxedUntil) {
        breathingRelaxedUntil = 0;
        pExprMgr->release(breathingToken);
        breathingToken = EXPR_TOKEN_INVALID;
        Serial.println("[Breathing] Relaxed animation ended");
    }
}

void ReactiveAnimations::triggerImuReaction(ImuEvent event) {
    uint32_t now = millis();

    // Cancel joy
    cancelJoy();

    Expression expr;
    const char* sound;
    bool forceNew = false;

    switch (event) {
        case ImuEvent::PickedUp:
            expr = Expression::Scared;
            sound = "/pick up.mp3";
            forceNew = true;
            break;
        case ImuEvent::ShookHard:
            expr = Expression::Dazed;
            sound = "/confused.mp3";
            break;
        case ImuEvent::Knocked:
            expr = Expression::Dizzy;
            sound = "/confused.mp3";
            break;
        default:
            return;
    }

    if (forceNew) {
        if (imuToken != EXPR_TOKEN_INVALID) pExprMgr->release(imuToken);
        imuToken = pExprMgr->request(expr, ExprPriority::IMU);
    } else {
        if (imuToken != EXPR_TOKEN_INVALID) pExprMgr->update(imuToken, expr);
        else imuToken = pExprMgr->request(expr, ExprPriority::IMU);
    }

    imuReacting = true;
    imuReactionStart = now;

    if (pAudioPlayer) pAudioPlayer->play(sound);
    Serial.printf("IMU reaction: %d\n", (int)event);
}

void ReactiveAnimations::triggerYawn() {
    uint32_t now = millis();

    cancelJoy();

    if (imuToken != EXPR_TOKEN_INVALID) pExprMgr->release(imuToken);
    imuToken = pExprMgr->request(Expression::Yawn, ExprPriority::IMU);
    imuReacting = true;
    imuReactionStart = now;

    Serial.println("Yawn triggered");
}

void ReactiveAnimations::triggerIrritated(uint32_t now) {
    irritatedToken = pExprMgr->request(Expression::Grumpy, ExprPriority::Behavior);
    showingIrritated = true;
    irritatedStart = now;

    cancelJoy();

    if (pMoodDrift) pMoodDrift->notifyLoudNoise(now);
    Serial.println("Too loud! Showing irritated expression");
}

void ReactiveAnimations::triggerLove(uint32_t now) {
    loveToken = pExprMgr->request(Expression::Love, ExprPriority::Behavior);
    showingLove = true;
    loveStart = now;
    Serial.println("Petting ended - showing hearts");
}

void ReactiveAnimations::triggerJoy(uint32_t now) {
    joyToken = pExprMgr->request(Expression::Joy, ExprPriority::Behavior);
    showingJoy = true;
    joyStart = now;
    bouncePhase = 0.0f;

    scheduleNextJoy(now);

    if (pAudioPlayer) pAudioPlayer->play("/joy.mp3");
    Serial.printf("Joy triggered! Next joy in %lu minutes\n", (nextJoyTime - now) / 60000);
}

void ReactiveAnimations::cancelJoy() {
    if (showingJoy) {
        showingJoy = false;
        if (pExprMgr && pExprMgr->isActive(joyToken)) {
            pExprMgr->release(joyToken);
        }
        joyToken = EXPR_TOKEN_INVALID;
    }
}

bool ReactiveAnimations::shouldTriggerJoy(uint32_t now) const {
    return !showingJoy && now >= nextJoyTime;
}

void ReactiveAnimations::scheduleNextJoy(uint32_t now) {
    nextJoyTime = now + JOY_MIN_INTERVAL + random(JOY_MAX_INTERVAL - JOY_MIN_INTERVAL);
}

void ReactiveAnimations::orientationChanged(Orientation orient, bool isPetted) {
    if (orient == lastOrientation) return;

    if (orient == Orientation::FaceDown && !isPetted && !imuReacting) {
        if (orientationToken != EXPR_TOKEN_INVALID) {
            pExprMgr->update(orientationToken, Expression::Sleepy);
        } else {
            orientationToken = pExprMgr->request(Expression::Sleepy, ExprPriority::Behavior);
        }
        Serial.println("Face-down - showing hiding expression");
    } else if (orient == Orientation::TiltedLong && !isPetted && !imuReacting) {
        if (orientationToken != EXPR_TOKEN_INVALID) {
            pExprMgr->update(orientationToken, Expression::Squint);
        } else {
            orientationToken = pExprMgr->request(Expression::Squint, ExprPriority::Behavior);
        }
        Serial.println("Tilted long - showing uncomfortable expression");
    } else if (orient == Orientation::Normal && pExprMgr->isActive(orientationToken)) {
        pExprMgr->release(orientationToken);
        orientationToken = EXPR_TOKEN_INVALID;
        Serial.println("Orientation normal - reverting expression");
    }

    lastOrientation = orient;
}

void ReactiveAnimations::notifyBreathingComplete(uint32_t now, bool soundEnabled) {
    breathingCompleteTime = now;
    breathingCompletePending = true;
    breathingSoundEnabled = soundEnabled;
}

bool ReactiveAnimations::isAnyActive() const {
    return imuReacting || showingIrritated || showingLove || showingJoy;
}
