/**
 * @file text_renderer.cpp
 * @brief Shared 5x7 bitmap font and drawing utilities
 *
 * All rendering applies 90° CCW rotation:
 *   buffer_x = screen_y
 *   buffer_y = bufH - 1 - screen_x
 */

#include "text_renderer.h"
#include <string.h>

// 5x7 bitmap font — superset of all glyphs used across the project
// Each glyph is 5 columns of 7-bit bitmaps (LSB = top row)
static const uint8_t FONT_5X7[][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x00, 0x00, 0x00, 0x00}, // (space, index 10)
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A (index 11)
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z (index 36)
    {0x00, 0x36, 0x36, 0x00, 0x00}, // : (colon, index 37)
    {0x00, 0x00, 0x40, 0x00, 0x00}, // . (period, index 38)
    {0x08, 0x08, 0x08, 0x08, 0x08}, // - (dash, index 39)
    {0x20, 0x10, 0x08, 0x04, 0x02}, // / (slash, index 40)
    {0x00, 0x60, 0x60, 0x00, 0x00}, // ' (apostrophe, index 41)
    {0x02, 0x01, 0x51, 0x09, 0x06}, // ? (question mark, index 42)
    {0x00, 0x00, 0x4F, 0x00, 0x00}, // ! (exclamation, index 43)
};

static const int FONT_GLYPH_COUNT = sizeof(FONT_5X7) / sizeof(FONT_5X7[0]);

// Map ASCII character to font index. Returns -1 if unsupported.
static int charToFontIndex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c == ' ') return 10;
    if (c >= 'A' && c <= 'Z') return 11 + (c - 'A');
    if (c >= 'a' && c <= 'z') return 11 + (c - 'a');  // lowercase → uppercase
    if (c == ':') return 37;
    if (c == '.') return 38;
    if (c == '-') return 39;
    if (c == '/') return 40;
    if (c == '\'') return 41;
    if (c == '?') return 42;
    if (c == '!') return 43;
    return -1;
}

namespace TextRenderer {

void drawFilledRect(uint16_t* buf, int16_t bufW, int16_t bufH,
                    int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    for (int16_t sy = y; sy < y + h; sy++) {
        for (int16_t sx = x; sx < x + w; sx++) {
            int16_t bx = sy;
            int16_t by = bufH - 1 - sx;
            if (bx >= 0 && bx < bufW && by >= 0 && by < bufH) {
                buf[by * bufW + bx] = color;
            }
        }
    }
}

void drawChar(uint16_t* buf, int16_t bufW, int16_t bufH,
              int16_t x, int16_t y, char c, uint16_t color, int scale) {
    int fontIdx = charToFontIndex(c);
    if (fontIdx < 0 || fontIdx >= FONT_GLYPH_COUNT) return;

    const uint8_t* charData = FONT_5X7[fontIdx];
    for (int col = 0; col < 5; col++) {
        uint8_t colBits = charData[col];
        for (int row = 0; row < 7; row++) {
            if (colBits & (1 << row)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int16_t screenX = x + col * scale + sx;
                        int16_t screenY = y + row * scale + sy;
                        int16_t bx = screenY;
                        int16_t by = bufH - 1 - screenX;
                        if (bx >= 0 && bx < bufW && by >= 0 && by < bufH) {
                            buf[by * bufW + bx] = color;
                        }
                    }
                }
            }
        }
    }
}

void drawText(uint16_t* buf, int16_t bufW, int16_t bufH,
              int16_t x, int16_t y, const char* text, uint16_t color, int scale) {
    int charWidth = 5 * scale + scale;  // glyph pixels + spacing
    int16_t curX = x;
    while (*text) {
        drawChar(buf, bufW, bufH, curX, y, *text, color, scale);
        curX += charWidth;
        text++;
    }
}

void drawCenteredText(uint16_t* buf, int16_t bufW, int16_t bufH,
                      int16_t centerX, int16_t y, const char* text,
                      uint16_t color, int scale) {
    int len = strlen(text);
    int charWidth = 5 * scale + scale;
    int16_t totalWidth = len * charWidth;
    int16_t x = centerX - totalWidth / 2;
    drawText(buf, bufW, bufH, x, y, text, color, scale);
}

void drawWrappedText(uint16_t* buf, int16_t bufW, int16_t bufH,
                     int16_t centerX, int16_t startY, const char* text,
                     uint16_t color, int scale, int maxCharsPerLine) {
    int len = strlen(text);
    int charWidth = 5 * scale + scale;
    int lineHeight = 7 * scale + scale * 2;

    int lineStart = 0;
    int lineCount = 0;
    char lineBuf[128];  // large enough for any reminder message

    while (lineStart < len && lineCount < 4) {
        int lineEnd = lineStart + maxCharsPerLine;
        if (lineEnd >= len) {
            lineEnd = len;
        } else {
            int lastSpace = lineEnd;
            while (lastSpace > lineStart && text[lastSpace] != ' ') {
                lastSpace--;
            }
            if (lastSpace > lineStart) {
                lineEnd = lastSpace;
            }
        }

        int lineLen = lineEnd - lineStart;
        if (lineLen > (int)sizeof(lineBuf) - 1) lineLen = sizeof(lineBuf) - 1;
        strncpy(lineBuf, text + lineStart, lineLen);
        lineBuf[lineLen] = '\0';

        const char* trimmed = lineBuf;
        while (*trimmed == ' ') trimmed++;

        int16_t y = startY + lineCount * lineHeight;
        drawCenteredText(buf, bufW, bufH, centerX, y, trimmed, color, scale);

        lineStart = lineEnd;
        if (lineStart < len && text[lineStart] == ' ') lineStart++;
        lineCount++;
    }
}

void drawLargeDigit(uint16_t* buf, int16_t bufW, int16_t bufH,
                    int16_t x, int16_t y, int digit, uint16_t color, int scale) {
    if (digit < 0 || digit > 9) return;
    // Reuse drawChar with the digit character
    drawChar(buf, bufW, bufH, x, y, '0' + digit, color, scale);
}

void clearBuffer(uint16_t* buf, int16_t bufW, int16_t bufH, uint16_t color) {
    int pixels = bufW * bufH;
    for (int i = 0; i < pixels; i++) {
        buf[i] = color;
    }
}

} // namespace TextRenderer
