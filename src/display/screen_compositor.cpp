/**
 * @file screen_compositor.cpp
 * @brief Framebuffer management and rendering utilities implementation
 */

#include "screen_compositor.h"

#define EYE_SPACING 120

ScreenCompositor::ScreenCompositor()
    : pGfx(nullptr)
    , pRenderer(nullptr)
    , eyeBuffer(nullptr)
    , prevLeftRect{0, 0, 0, 0, false}
    , prevRightRect{0, 0, 0, 0, false}
    , prevFrameWasMenu(false)
    , lastRenderedFilledLen(-1)
    , lastRenderMode(0)
    , needFullScreenClear(false)
{
}

bool ScreenCompositor::begin(Arduino_TFT* display, EyeRenderer& renderer) {
    pGfx = display;
    pRenderer = &renderer;

    // Allocate combined eye buffer in PSRAM
    size_t bufSize = COMBINED_BUF_WIDTH * COMBINED_BUF_HEIGHT * sizeof(uint16_t);
    eyeBuffer = (uint16_t *)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);

    if (!eyeBuffer) {
        Serial.println("PSRAM alloc failed, using internal RAM");
        eyeBuffer = (uint16_t *)malloc(bufSize);
    }

    if (!eyeBuffer) {
        Serial.println("Buffer alloc failed!");
        return false;
    }

    Serial.printf("Combined eye buffer: %dx%d (%d bytes)\n",
                  COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT, bufSize);

    initEyePositions();
    return true;
}

void ScreenCompositor::initEyePositions() {
    // Position buffer inside 16px progress bar margins
    leftEyePos.bufX = 16;
    leftEyePos.bufY = 16;

    // Eye center positions within combined buffer
    leftEyePos.baseX = COMBINED_BUF_WIDTH / 2;
    leftEyePos.baseY = COMBINED_BUF_HEIGHT / 2 - EYE_SPACING / 2;

    rightEyePos.baseX = COMBINED_BUF_WIDTH / 2;
    rightEyePos.baseY = COMBINED_BUF_HEIGHT / 2 + EYE_SPACING / 2;

    // Same screen position for both (single combined buffer)
    rightEyePos.bufX = leftEyePos.bufX;
    rightEyePos.bufY = leftEyePos.bufY;

    Serial.printf("Combined buffer: %dx%d at screen (%d,%d)\n",
                  COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT,
                  leftEyePos.bufX, leftEyePos.bufY);
    Serial.printf("Eye centers in buffer: L(%d,%d) R(%d,%d)\n",
                  leftEyePos.baseX, leftEyePos.baseY,
                  rightEyePos.baseX, rightEyePos.baseY);
}

// --- Buffer operations ---

void ScreenCompositor::clearBuffer() {
    pRenderer->clearBuffer(eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT);
}

void ScreenCompositor::clearRect(int16_t rx, int16_t ry, int16_t rw, int16_t rh) {
    if (rx < 0) { rw += rx; rx = 0; }
    if (ry < 0) { rh += ry; ry = 0; }
    if (rx + rw > COMBINED_BUF_WIDTH) rw = COMBINED_BUF_WIDTH - rx;
    if (ry + rh > COMBINED_BUF_HEIGHT) rh = COMBINED_BUF_HEIGHT - ry;
    if (rw <= 0 || rh <= 0) return;

    for (int16_t y = ry; y < ry + rh; y++) {
        memset(&eyeBuffer[y * COMBINED_BUF_WIDTH + rx], 0, rw * sizeof(uint16_t));
    }
}

// --- Render mode tracking ---

bool ScreenCompositor::updateRenderMode(int newMode) {
    if (newMode != lastRenderMode) {
        needFullScreenClear = true;
        Serial.printf("Render mode change: %d -> %d (full screen clear)\n", lastRenderMode, newMode);
        lastRenderMode = newMode;
    }

    if (needFullScreenClear) {
        pGfx->startWrite();
        pGfx->fillScreen(0);
        pGfx->endWrite();
        needFullScreenClear = false;
        prevLeftRect.valid = false;
        prevRightRect.valid = false;
        prevFrameWasMenu = false;
        lastRenderedFilledLen = -1;
        return true;
    }
    return false;
}

void ScreenCompositor::requestFullScreenClear() {
    needFullScreenClear = true;
}

// --- Dirty rect helpers ---

DirtyRect ScreenCompositor::computeEyeRect(const EyeShape& shape, int16_t centerX, int16_t centerY, int16_t margin) {
    int16_t ox = shape.getOffsetXPixels();
    int16_t oy = shape.getOffsetYPixels();
    int16_t eyeHeight = shape.getHeight();
    int16_t w, h;

    switch (shape.shapeType) {
        case ShapeType::Star: {
            int16_t outerR = (int16_t)(eyeHeight * 0.6f);
            w = h = outerR * 2;
            break;
        }
        case ShapeType::Heart: {
            int16_t heartSize = (int16_t)(eyeHeight * 0.5f);
            w = h = (int16_t)(heartSize * 3);
            break;
        }
        case ShapeType::Swirl: {
            int16_t swirlSize = (int16_t)(eyeHeight * 0.6f);
            w = h = swirlSize * 2;
            break;
        }
        case ShapeType::Circle: {
            int16_t circleR = (int16_t)(eyeHeight * 0.5f);
            w = h = circleR * 2;
            break;
        }
        default:  // Rectangle
            w = shape.getWidth();
            h = shape.getHeight();
            break;
    }

    DirtyRect r;
    r.x = centerX - w / 2 + ox - margin;
    r.y = centerY - h / 2 + oy - margin;
    r.w = w + margin * 2;
    r.h = h + margin * 2;
    r.valid = true;
    return r;
}

DirtyRect ScreenCompositor::unionRect(const DirtyRect& a, const DirtyRect& b) {
    if (!a.valid) return b;
    if (!b.valid) return a;
    DirtyRect u;
    int ax2 = a.x + a.w, ay2 = a.y + a.h;
    int bx2 = b.x + b.w, by2 = b.y + b.h;
    u.x = (a.x < b.x) ? a.x : b.x;
    u.y = (a.y < b.y) ? a.y : b.y;
    u.w = ((ax2 > bx2) ? ax2 : bx2) - u.x;
    u.h = ((ay2 > by2) ? ay2 : by2) - u.y;
    u.valid = true;
    return u;
}

bool ScreenCompositor::prepareEyeFrame(bool wasMenuOrFullScreen) {
    bool needFull = false;

    if (wasMenuOrFullScreen) {
        clearBuffer();
        prevFrameWasMenu = false;
        needFull = true;
        prevLeftRect.valid = false;
        prevRightRect.valid = false;
    } else if (prevLeftRect.valid || prevRightRect.valid) {
        // Clear only previous eye bounding boxes (with bounce margin)
        if (prevLeftRect.valid) {
            clearRect(prevLeftRect.x - 20, prevLeftRect.y - 5,
                      prevLeftRect.w + 40, prevLeftRect.h + 10);
        }
        if (prevRightRect.valid) {
            clearRect(prevRightRect.x - 20, prevRightRect.y - 5,
                      prevRightRect.w + 40, prevRightRect.h + 10);
        }
    } else {
        clearBuffer();
        needFull = true;
    }

    return needFull;
}

void ScreenCompositor::blitEyes(bool doFullBlit, const DirtyRect& curLeft, const DirtyRect& curRight) {
    if (doFullBlit) {
        fullBlit();
    } else {
        DirtyRect blitRect = unionRect(prevLeftRect, curLeft);
        blitRect = unionRect(blitRect, prevRightRect);
        blitRect = unionRect(blitRect, curRight);
        blitRect.x = (blitRect.x > 5) ? blitRect.x - 5 : 0;
        blitRect.y = (blitRect.y > 5) ? blitRect.y - 5 : 0;
        blitRect.w += 10;
        blitRect.h += 10;
        blitRegion(blitRect);
    }

    prevLeftRect = curLeft;
    prevRightRect = curRight;
}

void ScreenCompositor::invalidatePrevRects() {
    prevLeftRect.valid = false;
    prevRightRect.valid = false;
}

// --- Blit operations ---

void ScreenCompositor::fullBlit() {
    pGfx->startWrite();
    pGfx->draw16bitRGBBitmap(leftEyePos.bufX, leftEyePos.bufY,
                              eyeBuffer, COMBINED_BUF_WIDTH, COMBINED_BUF_HEIGHT);
    pGfx->endWrite();
}

void ScreenCompositor::blitRegion(const DirtyRect& region, bool manageWrite) {
    if (!region.valid) return;

    int16_t rx = (region.x > 0) ? region.x : 0;
    int16_t ry = (region.y > 0) ? region.y : 0;
    int rw_end = region.x + region.w;
    int rh_end = region.y + region.h;
    int16_t rw = ((rw_end < COMBINED_BUF_WIDTH) ? rw_end : COMBINED_BUF_WIDTH) - rx;
    int16_t rh = ((rh_end < COMBINED_BUF_HEIGHT) ? rh_end : COMBINED_BUF_HEIGHT) - ry;
    if (rw <= 0 || rh <= 0) return;

    int16_t screenX = leftEyePos.bufX + rx;
    int16_t screenY = leftEyePos.bufY + ry;

    if (manageWrite) pGfx->startWrite();
    pGfx->writeAddrWindow(screenX, screenY, rw, rh);
    for (int16_t y = 0; y < rh; y++) {
        pGfx->writePixels(&eyeBuffer[(ry + y) * COMBINED_BUF_WIDTH + rx], rw);
    }
    if (manageWrite) pGfx->endWrite();
}

void ScreenCompositor::blitSafeCenter(bool manageWrite) {
    // Blit only the central region that doesn't overlap 42px rounded corners
    const int16_t cornerMargin = 42 - 16;  // 26px offset from buffer edge
    const int16_t safeW = COMBINED_BUF_WIDTH - 2 * cornerMargin;
    const int16_t safeH = COMBINED_BUF_HEIGHT - 2 * cornerMargin;
    DirtyRect safeRegion = {cornerMargin, cornerMargin, safeW, safeH, true};
    blitRegion(safeRegion, manageWrite);
}

// --- Progress bars ---

void ScreenCompositor::renderPomodoroProgressBar(float progress, bool manageWrite, bool progressiveCorners) {
    const int16_t screenW = LCD_WIDTH;
    const int16_t screenH = LCD_HEIGHT;
    const int16_t barThick = 16;
    const int16_t cornerR = 42;

    uint16_t fillColor = pRenderer->getColor();
    uint16_t emptyColor = 0x2104;

    int halfLeftLen = (screenH / 2) - cornerR;
    int topLen = screenW - 2 * cornerR;
    int rightLen = screenH - 2 * cornerR;
    int bottomLen = screenW - 2 * cornerR;
    int otherHalfLeftLen = screenH - (screenH / 2) - cornerR;
    int cornerLen = (int)(1.57f * cornerR);
    int totalLen = halfLeftLen + bottomLen + rightLen + topLen + otherHalfLeftLen + 4 * cornerLen;
    int filledLen = (int)(progress * totalLen);

    if (manageWrite) pGfx->startWrite();

    int pos = 0;

    auto getColor = [&](int p) -> uint16_t {
        return (p < filledLen) ? fillColor : emptyColor;
    };

    float arcCenterR = cornerR - barThick / 2.0f;
    int arcSteps = 8;
    int arcCircleR = barThick / 2 + 3;

    auto drawCornerArc = [&](float startAngle, float endAngle, int16_t centerX, int16_t centerY,
                              uint16_t color, int cornerStartPos, int cornerLength) {
        for (int i = 0; i < arcSteps; i++) {
            float t = (float)i / (arcSteps - 1);
            float angle = startAngle + (endAngle - startAngle) * t;
            int16_t cx = centerX + (int16_t)(cosf(angle) * arcCenterR);
            int16_t cy = centerY + (int16_t)(sinf(angle) * arcCenterR);
            uint16_t circleColor = color;
            if (progressiveCorners) {
                int circlePos = cornerStartPos + (int)(t * cornerLength);
                circleColor = (circlePos < filledLen) ? fillColor : emptyColor;
            }
            pGfx->fillCircle(cx, cy, arcCircleR, circleColor);
        }
    };

    // Segment 1: Left edge, middle going DOWN
    {
        int segStart = pos;
        int segEnd = pos + halfLeftLen;
        if (filledLen >= segEnd) {
            pGfx->fillRect(0, screenH / 2, barThick, halfLeftLen, fillColor);
        } else if (filledLen <= segStart) {
            pGfx->fillRect(0, screenH / 2, barThick, halfLeftLen, emptyColor);
        } else {
            int fillPx = filledLen - segStart;
            pGfx->fillRect(0, screenH / 2, barThick, fillPx, fillColor);
            pGfx->fillRect(0, screenH / 2 + fillPx, barThick, halfLeftLen - fillPx, emptyColor);
        }
        pos = segEnd;
    }

    // Segment 2: Bottom-left corner arc
    {
        int segStart = pos;
        int segMid = pos + cornerLen / 2;
        uint16_t cornerColor = (filledLen >= segMid) ? fillColor : emptyColor;
        drawCornerArc(M_PI, M_PI / 2, cornerR, screenH - cornerR, cornerColor, segStart, cornerLen);
        pos += cornerLen;
    }

    // Segment 3: Bottom edge, left to right
    {
        int segStart = pos;
        int segEnd = pos + bottomLen;
        if (filledLen >= segEnd) {
            pGfx->fillRect(cornerR, screenH - barThick, bottomLen, barThick, fillColor);
        } else if (filledLen <= segStart) {
            pGfx->fillRect(cornerR, screenH - barThick, bottomLen, barThick, emptyColor);
        } else {
            int fillPx = filledLen - segStart;
            pGfx->fillRect(cornerR, screenH - barThick, fillPx, barThick, fillColor);
            pGfx->fillRect(cornerR + fillPx, screenH - barThick, bottomLen - fillPx, barThick, emptyColor);
        }
        pos = segEnd;
    }

    // Segment 4: Bottom-right corner arc
    {
        int segStart = pos;
        int segMid = pos + cornerLen / 2;
        uint16_t cornerColor = (filledLen >= segMid) ? fillColor : emptyColor;
        drawCornerArc(M_PI / 2, 0, screenW - cornerR, screenH - cornerR, cornerColor, segStart, cornerLen);
        pos += cornerLen;
    }

    // Segment 5: Right edge, bottom to top
    {
        int segStart = pos;
        int segEnd = pos + rightLen;
        int16_t edgeX = screenW - barThick;
        int16_t startY = screenH - cornerR;
        if (filledLen >= segEnd) {
            pGfx->fillRect(edgeX, cornerR, barThick, rightLen, fillColor);
        } else if (filledLen <= segStart) {
            pGfx->fillRect(edgeX, cornerR, barThick, rightLen, emptyColor);
        } else {
            int fillPx = filledLen - segStart;
            pGfx->fillRect(edgeX, startY - fillPx, barThick, fillPx, fillColor);
            pGfx->fillRect(edgeX, cornerR, barThick, rightLen - fillPx, emptyColor);
        }
        pos = segEnd;
    }

    // Segment 6: Top-right corner arc
    {
        int segStart = pos;
        int segMid = pos + cornerLen / 2;
        uint16_t cornerColor = (filledLen >= segMid) ? fillColor : emptyColor;
        drawCornerArc(0, -M_PI / 2, screenW - cornerR, cornerR, cornerColor, segStart, cornerLen);
        pos += cornerLen;
    }

    // Segment 7: Top edge, right to left
    {
        int segStart = pos;
        int segEnd = pos + topLen;
        if (filledLen >= segEnd) {
            pGfx->fillRect(cornerR, 0, topLen, barThick, fillColor);
        } else if (filledLen <= segStart) {
            pGfx->fillRect(cornerR, 0, topLen, barThick, emptyColor);
        } else {
            int fillPx = filledLen - segStart;
            pGfx->fillRect(screenW - cornerR - fillPx, 0, fillPx, barThick, fillColor);
            pGfx->fillRect(cornerR, 0, topLen - fillPx, barThick, emptyColor);
        }
        pos = segEnd;
    }

    // Segment 8: Top-left corner arc
    {
        int segStart = pos;
        int segMid = pos + cornerLen / 2;
        uint16_t cornerColor = (filledLen >= segMid) ? fillColor : emptyColor;
        drawCornerArc(-M_PI / 2, -M_PI, cornerR, cornerR, cornerColor, segStart, cornerLen);
        pos += cornerLen;
    }

    // Segment 9: Left edge, top to middle
    {
        int segStart = pos;
        int segEnd = pos + otherHalfLeftLen;
        if (filledLen >= segEnd) {
            pGfx->fillRect(0, cornerR, barThick, otherHalfLeftLen, fillColor);
        } else if (filledLen <= segStart) {
            pGfx->fillRect(0, cornerR, barThick, otherHalfLeftLen, emptyColor);
        } else {
            int fillPx = filledLen - segStart;
            pGfx->fillRect(0, cornerR, barThick, fillPx, fillColor);
            pGfx->fillRect(0, cornerR + fillPx, barThick, otherHalfLeftLen - fillPx, emptyColor);
        }
    }

    if (manageWrite) pGfx->endWrite();
}

void ScreenCompositor::renderBreathingProgressBar(float progress, float pulseBlend, bool reverse) {
    const int16_t screenW = LCD_WIDTH;
    const int16_t screenH = LCD_HEIGHT;
    const int16_t barThick = 16;
    const int16_t cornerR = 42;

    uint16_t eyeColor = pRenderer->getColor();
    uint16_t emptyColor = 0x2104;

    // Interpolate fill color based on pulseBlend
    uint16_t r1 = (eyeColor >> 11) & 0x1F;
    uint16_t g1 = (eyeColor >> 5) & 0x3F;
    uint16_t b1 = eyeColor & 0x1F;
    uint16_t r2 = (emptyColor >> 11) & 0x1F;
    uint16_t g2 = (emptyColor >> 5) & 0x3F;
    uint16_t b2 = emptyColor & 0x1F;
    uint16_t r = r1 + (int16_t)((r2 - r1) * pulseBlend);
    uint16_t g = g1 + (int16_t)((g2 - g1) * pulseBlend);
    uint16_t b = b1 + (int16_t)((b2 - b1) * pulseBlend);
    uint16_t fillColor = (r << 11) | (g << 5) | b;

    int halfLeftLen = (screenH / 2) - cornerR;
    int topLen = screenW - 2 * cornerR;
    int rightLen = screenH - 2 * cornerR;
    int bottomLen = screenW - 2 * cornerR;
    int otherHalfLeftLen = screenH - (screenH / 2) - cornerR;
    int cornerLen = (int)(1.57f * cornerR);
    int totalLen = halfLeftLen + bottomLen + rightLen + topLen + otherHalfLeftLen + 4 * cornerLen;
    int filledLen = (int)(progress * totalLen);

    int fillStart = reverse ? (totalLen - filledLen) : 0;
    int fillEnd = reverse ? totalLen : filledLen;

    pGfx->startWrite();

    int pos = 0;

    float arcCenterR = cornerR - barThick / 2.0f;
    int arcSteps = 8;
    int arcCircleR = barThick / 2 + 3;

    auto isFilled = [&](int p) -> bool {
        return p >= fillStart && p < fillEnd;
    };

    auto drawCornerArc = [&](float startAngle, float endAngle, int16_t centerX, int16_t centerY,
                              int cornerStartPos, int cornerLength) {
        for (int i = 0; i < arcSteps; i++) {
            float t = (float)i / (arcSteps - 1);
            float angle = startAngle + (endAngle - startAngle) * t;
            int16_t cx = centerX + (int16_t)(cosf(angle) * arcCenterR);
            int16_t cy = centerY + (int16_t)(sinf(angle) * arcCenterR);
            int circlePos = cornerStartPos + (int)(t * cornerLength);
            uint16_t circleColor = isFilled(circlePos) ? fillColor : emptyColor;
            pGfx->fillCircle(cx, cy, arcCircleR, circleColor);
        }
    };

    auto getSegmentFill = [&](int segStart, int segEnd) -> std::pair<int, int> {
        int overlapStart = max(segStart, fillStart);
        int overlapEnd = min(segEnd, fillEnd);
        if (overlapStart >= overlapEnd) {
            return {0, 0};
        }
        return {overlapStart - segStart, overlapEnd - overlapStart};
    };

    // Segment 1: Left edge, middle going down
    {
        int segStart = pos;
        int segEnd = pos + halfLeftLen;
        auto [fillOffset, fillPx] = getSegmentFill(segStart, segEnd);
        if (fillPx == halfLeftLen) {
            pGfx->fillRect(0, screenH / 2, barThick, halfLeftLen, fillColor);
        } else if (fillPx == 0) {
            pGfx->fillRect(0, screenH / 2, barThick, halfLeftLen, emptyColor);
        } else {
            if (fillOffset > 0)
                pGfx->fillRect(0, screenH / 2, barThick, fillOffset, emptyColor);
            pGfx->fillRect(0, screenH / 2 + fillOffset, barThick, fillPx, fillColor);
            int afterFill = halfLeftLen - fillOffset - fillPx;
            if (afterFill > 0)
                pGfx->fillRect(0, screenH / 2 + fillOffset + fillPx, barThick, afterFill, emptyColor);
        }
        pos = segEnd;
    }

    // Segment 2: Bottom-left corner
    {
        int segStart = pos;
        drawCornerArc(M_PI, M_PI / 2, cornerR, screenH - cornerR, segStart, cornerLen);
        pos += cornerLen;
    }

    // Segment 3: Bottom edge
    {
        int segStart = pos;
        int segEnd = pos + bottomLen;
        auto [fillOffset, fillPx] = getSegmentFill(segStart, segEnd);
        if (fillPx == bottomLen) {
            pGfx->fillRect(cornerR, screenH - barThick, bottomLen, barThick, fillColor);
        } else if (fillPx == 0) {
            pGfx->fillRect(cornerR, screenH - barThick, bottomLen, barThick, emptyColor);
        } else {
            if (fillOffset > 0)
                pGfx->fillRect(cornerR, screenH - barThick, fillOffset, barThick, emptyColor);
            pGfx->fillRect(cornerR + fillOffset, screenH - barThick, fillPx, barThick, fillColor);
            int afterFill = bottomLen - fillOffset - fillPx;
            if (afterFill > 0)
                pGfx->fillRect(cornerR + fillOffset + fillPx, screenH - barThick, afterFill, barThick, emptyColor);
        }
        pos = segEnd;
    }

    // Segment 4: Bottom-right corner
    {
        int segStart = pos;
        drawCornerArc(M_PI / 2, 0, screenW - cornerR, screenH - cornerR, segStart, cornerLen);
        pos += cornerLen;
    }

    // Segment 5: Right edge (bottom to top)
    {
        int segStart = pos;
        int segEnd = pos + rightLen;
        int16_t edgeX = screenW - barThick;
        int16_t bottomY = screenH - cornerR;
        auto [fillOffset, fillPx] = getSegmentFill(segStart, segEnd);
        if (fillPx == rightLen) {
            pGfx->fillRect(edgeX, cornerR, barThick, rightLen, fillColor);
        } else if (fillPx == 0) {
            pGfx->fillRect(edgeX, cornerR, barThick, rightLen, emptyColor);
        } else {
            int beforeFill = fillOffset;
            int afterFill = rightLen - fillOffset - fillPx;
            if (afterFill > 0)
                pGfx->fillRect(edgeX, cornerR, barThick, afterFill, emptyColor);
            pGfx->fillRect(edgeX, cornerR + afterFill, barThick, fillPx, fillColor);
            if (beforeFill > 0)
                pGfx->fillRect(edgeX, bottomY - beforeFill, barThick, beforeFill, emptyColor);
        }
        pos = segEnd;
    }

    // Segment 6: Top-right corner
    {
        int segStart = pos;
        drawCornerArc(0, -M_PI / 2, screenW - cornerR, cornerR, segStart, cornerLen);
        pos += cornerLen;
    }

    // Segment 7: Top edge (right to left)
    {
        int segStart = pos;
        int segEnd = pos + topLen;
        int16_t rightX = screenW - cornerR;
        auto [fillOffset, fillPx] = getSegmentFill(segStart, segEnd);
        if (fillPx == topLen) {
            pGfx->fillRect(cornerR, 0, topLen, barThick, fillColor);
        } else if (fillPx == 0) {
            pGfx->fillRect(cornerR, 0, topLen, barThick, emptyColor);
        } else {
            int beforeFill = fillOffset;
            int afterFill = topLen - fillOffset - fillPx;
            if (afterFill > 0)
                pGfx->fillRect(cornerR, 0, afterFill, barThick, emptyColor);
            pGfx->fillRect(cornerR + afterFill, 0, fillPx, barThick, fillColor);
            if (beforeFill > 0)
                pGfx->fillRect(rightX - beforeFill, 0, beforeFill, barThick, emptyColor);
        }
        pos = segEnd;
    }

    // Segment 8: Top-left corner
    {
        int segStart = pos;
        drawCornerArc(-M_PI / 2, -M_PI, cornerR, cornerR, segStart, cornerLen);
        pos += cornerLen;
    }

    // Segment 9: Left edge, top to middle
    {
        int segStart = pos;
        int segEnd = pos + otherHalfLeftLen;
        auto [fillOffset, fillPx] = getSegmentFill(segStart, segEnd);
        if (fillPx == otherHalfLeftLen) {
            pGfx->fillRect(0, cornerR, barThick, otherHalfLeftLen, fillColor);
        } else if (fillPx == 0) {
            pGfx->fillRect(0, cornerR, barThick, otherHalfLeftLen, emptyColor);
        } else {
            if (fillOffset > 0)
                pGfx->fillRect(0, cornerR, barThick, fillOffset, emptyColor);
            pGfx->fillRect(0, cornerR + fillOffset, barThick, fillPx, fillColor);
            int afterFill = otherHalfLeftLen - fillOffset - fillPx;
            if (afterFill > 0)
                pGfx->fillRect(0, cornerR + fillOffset + fillPx, barThick, afterFill, emptyColor);
        }
    }

    pGfx->endWrite();
}

void ScreenCompositor::clearProgressBarEdges() {
    const int16_t screenW = LCD_WIDTH;
    const int16_t screenH = LCD_HEIGHT;
    const int16_t barThick = 16;
    const int16_t cornerR = 42;

    pGfx->startWrite();
    pGfx->fillRect(0, 0, screenW, barThick, 0);
    pGfx->fillRect(0, screenH - barThick, screenW, barThick, 0);
    pGfx->fillRect(0, 0, barThick, screenH, 0);
    pGfx->fillRect(screenW - barThick, 0, barThick, screenH, 0);
    pGfx->fillRect(0, 0, cornerR + 5, cornerR + 5, 0);
    pGfx->fillRect(screenW - cornerR - 5, 0, cornerR + 5, cornerR + 5, 0);
    pGfx->fillRect(0, screenH - cornerR - 5, cornerR + 5, cornerR + 5, 0);
    pGfx->fillRect(screenW - cornerR - 5, screenH - cornerR - 5, cornerR + 5, cornerR + 5, 0);
    pGfx->endWrite();
}

// --- Sleep mode ---

void ScreenCompositor::renderSleepBars(float brightness) {
    uint8_t r = (uint8_t)(0 * brightness);
    uint8_t g = (uint8_t)(200 * brightness);
    uint8_t b = (uint8_t)(255 * brightness);
    uint16_t barColor = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

    clearBuffer();

    int16_t barThickness = 6;
    int16_t barLength = BASE_EYE_HEIGHT * 3 / 4;
    int16_t centerX = COMBINED_BUF_WIDTH / 2;

    // Left eye bar
    int16_t leftBarStartY = leftEyePos.baseY - barLength / 2;
    int16_t leftBarStartX = centerX - barThickness / 2;
    for (int16_t y = leftBarStartY; y < leftBarStartY + barLength; y++) {
        for (int16_t x = leftBarStartX; x < leftBarStartX + barThickness; x++) {
            if (x >= 0 && x < COMBINED_BUF_WIDTH && y >= 0 && y < COMBINED_BUF_HEIGHT) {
                eyeBuffer[y * COMBINED_BUF_WIDTH + x] = barColor;
            }
        }
    }

    // Right eye bar
    int16_t rightBarStartY = rightEyePos.baseY - barLength / 2;
    int16_t rightBarStartX = centerX - barThickness / 2;
    for (int16_t y = rightBarStartY; y < rightBarStartY + barLength; y++) {
        for (int16_t x = rightBarStartX; x < rightBarStartX + barThickness; x++) {
            if (x >= 0 && x < COMBINED_BUF_WIDTH && y >= 0 && y < COMBINED_BUF_HEIGHT) {
                eyeBuffer[y * COMBINED_BUF_WIDTH + x] = barColor;
            }
        }
    }

    fullBlit();
}
