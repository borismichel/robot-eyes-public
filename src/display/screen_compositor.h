/**
 * @file screen_compositor.h
 * @brief Framebuffer management, eye positioning, and rendering utilities
 *
 * Owns the PSRAM framebuffer, eye position geometry, dirty rect tracking,
 * progress bar rendering, and screen blit operations.
 */

#ifndef SCREEN_COMPOSITOR_H
#define SCREEN_COMPOSITOR_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "../eyes/eye_renderer.h"
#include "../eyes/eye_shape.h"
#include "../../include/pin_config.h"

// Forward declarations
class EyeRenderer;

// Eye positioning on screen
struct EyePosition {
    int16_t baseX, baseY;   // Base center position in buffer
    int16_t bufX, bufY;     // Top-left of buffer region on screen
};

// Dirty-rect tracking for partial screen updates
struct DirtyRect {
    int16_t x, y, w, h;
    bool valid;
};

class ScreenCompositor {
public:
    ScreenCompositor();

    /**
     * @brief Allocate PSRAM framebuffer and initialize eye positions
     * @return true if allocation succeeded
     */
    bool begin(Arduino_TFT* display, EyeRenderer& renderer);

    // --- Buffer access ---
    uint16_t* getBuffer() { return eyeBuffer; }
    void clearBuffer();
    void clearRect(int16_t x, int16_t y, int16_t w, int16_t h);

    // --- Eye positions ---
    const EyePosition& getLeftEyePos() const { return leftEyePos; }
    const EyePosition& getRightEyePos() const { return rightEyePos; }

    // --- Render mode tracking ---
    /**
     * @brief Check for render mode change and execute full screen clear if needed
     * @return true if full screen clear was performed
     */
    bool updateRenderMode(int newMode);
    void requestFullScreenClear();

    // --- Dirty rect helpers ---
    DirtyRect computeEyeRect(const EyeShape& shape, int16_t centerX, int16_t centerY, int16_t margin = 10);
    static DirtyRect unionRect(const DirtyRect& a, const DirtyRect& b);

    /**
     * @brief Prepare buffer for eye rendering frame (dirty rect clearing)
     * @param wasMenuOrFullScreen true if previous frame was menu/countdown/time display
     * @return true if full blit is needed
     */
    bool prepareEyeFrame(bool wasMenuOrFullScreen);

    /**
     * @brief Blit eyes to screen using dirty rects (full or partial)
     */
    void blitEyes(bool fullBlit, const DirtyRect& curLeft, const DirtyRect& curRight);

    // --- Blit operations ---
    void fullBlit();
    void blitRegion(const DirtyRect& region, bool manageWrite = true);
    void blitSafeCenter(bool manageWrite = true);

    // --- Progress bars ---
    void renderPomodoroProgressBar(float progress, bool manageWrite = true, bool progressiveCorners = false);
    void renderBreathingProgressBar(float progress, float pulseBlend = 0.0f, bool reverse = false);
    void clearProgressBarEdges();

    // --- Sleep mode ---
    void renderSleepBars(float brightness);

    // --- Dirty rect state (public for main.cpp to set wasMenu flag) ---
    bool prevFrameWasMenu;
    void invalidatePrevRects();

    // --- Progress bar cache ---
    int lastRenderedFilledLen;

private:
    Arduino_TFT* pGfx;
    EyeRenderer* pRenderer;

    uint16_t* eyeBuffer;
    EyePosition leftEyePos, rightEyePos;

    DirtyRect prevLeftRect, prevRightRect;

    int lastRenderMode;
    bool needFullScreenClear;

    void initEyePositions();
};

#endif // SCREEN_COMPOSITOR_H
