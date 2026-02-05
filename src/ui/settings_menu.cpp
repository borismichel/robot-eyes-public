/**
 * @file settings_menu.cpp
 * @brief Full-screen settings with swipeable pages
 *
 * Buffer is 360x435 (COMBINED_BUF_WIDTH x COMBINED_BUF_HEIGHT).
 * Visible screen is 435 wide x 360 tall (landscape) after 90° CCW rotation.
 *
 * Rotation mapping (90° CCW):
 *   screen (sx, sy) → buffer (sy, bufH - 1 - sx)
 */

#include "settings_menu.h"
#include "pomodoro.h"
#include "../behavior/breathing_exercise.h"
#include <cmath>
#include <time.h>

// Colors (RGB565)
#define BG_COLOR           0x0000  // Black background
#define SLIDER_BG_COLOR    0x2104  // Dark gray for track
#define SLIDER_FILL_COLOR  0x07FF  // Cyan (matches eyes)
#define KNOB_COLOR         0xFFFF  // White
#define TEXT_COLOR         0xFFFF  // White
#define ARROW_COLOR        0x4A49  // Gray for navigation hints

// Visible screen dimensions (after rotation) - matches COMBINED_BUF dimensions
#define SCREEN_W 416  // buffer height (COMBINED_BUF_HEIGHT) becomes screen width
#define SCREEN_H 336  // buffer width (COMBINED_BUF_WIDTH) becomes screen height

// Simple 5x7 font data
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
    {0x00, 0x36, 0x36, 0x00, 0x00}, // : (index 37)
    {0x08, 0x1C, 0x3E, 0x7F, 0x00}, // up arrow (index 38)
    {0x00, 0x7F, 0x3E, 0x1C, 0x08}, // down arrow (index 39)
    {0x14, 0x14, 0x14, 0x14, 0x14}, // - (index 40)
};

const char* SettingsMenu::mainPageLabels[NUM_MAIN_PAGES] = {
    "POMODORO",
    "MINDFUL",
    "SETTINGS",
    "EXIT"
};

const char* SettingsMenu::pomoPageLabels[POMO_NUM_PAGES] = {
    "START",      // or "STOP" when running
    "WORK",
    "SHORT BRK",
    "LONG BRK",
    "SESSIONS",
    "TICKING",
    "BACK"
};

const char* SettingsMenu::mindfulPageLabels[MINDFUL_NUM_PAGES] = {
    "BREATHE",
    "SCHEDULE",
    "SOUND",
    "INTERVAL",
    "BACK"
};

const char* SettingsMenu::settingsPageLabels[SETTINGS_NUM_PAGES] = {
    "VOLUME",
    "BRIGHT",
    "MIC GAIN",
    "MIC THR",
    "COLOR",
    "TIME",
    "12-24H",
    "TIMEZONE",
    "WIFI",
    "BACK"
};

SettingsMenu::SettingsMenu()
    : menuOpen(false)
    , currentPage(0)
    , pomodoroTimer(nullptr)
    , breathingExercise(nullptr)
    , colorIndex(0)
    , timeHour(12)
    , timeMinute(0)
    , is24Hour(false)
    , gmtOffsetHours(0)
    , wifiEnabled(true)
    , offlineModeConfigured(false)
    , settingsVersion(0)
    , pomoSubMenuOpen(false)
    , pomoSubPage(0)
    , settingsSubMenuOpen(false)
    , settingsSubPage(0)
    , mindfulSubMenuOpen(false)
    , mindfulSubPage(0)
    , wasTouched(false)
    , touchStartX(0)
    , touchStartY(0)
    , touchCurrentY(0)
    , isDraggingSlider(false)
    , isSwiping(false) {
    values[0] = 80;   // Volume
    values[1] = 100;  // Brightness
    values[2] = 50;   // Mic Gain
    values[3] = 50;   // Mic Threshold (0.5)
}

void SettingsMenu::begin() {
    loadSettings();
}

void SettingsMenu::open() {
    menuOpen = true;
    currentPage = 0;
    Serial.println("Settings menu opened");
}

void SettingsMenu::close() {
    menuOpen = false;
    pomoSubMenuOpen = false;
    pomoSubPage = 0;
    settingsSubMenuOpen = false;
    settingsSubPage = 0;
    saveSettings();
    Serial.println("Settings menu closed");
}

void SettingsMenu::toggle() {
    if (menuOpen) {
        close();
    } else {
        open();
    }
}

void SettingsMenu::nextPage() {
    if (currentPage < NUM_MAIN_PAGES - 1) {
        currentPage++;
        Serial.printf("Main page: %d (%s)\n", currentPage, mainPageLabels[currentPage]);
    }
}

void SettingsMenu::prevPage() {
    if (currentPage > 0) {
        currentPage--;
        Serial.printf("Main page: %d (%s)\n", currentPage, mainPageLabels[currentPage]);
    }
}

void SettingsMenu::openPomoSubMenu() {
    pomoSubMenuOpen = true;
    pomoSubPage = POMO_PAGE_START_STOP;
    Serial.println("Pomodoro sub-menu opened");
}

void SettingsMenu::closePomoSubMenu() {
    pomoSubMenuOpen = false;
    pomoSubPage = 0;
    Serial.println("Pomodoro sub-menu closed");
}

void SettingsMenu::pomoNextPage() {
    if (pomoSubPage < POMO_NUM_PAGES - 1) {
        pomoSubPage++;
        Serial.printf("Pomo sub-page: %d (%s)\n", pomoSubPage, pomoPageLabels[pomoSubPage]);
    }
}

void SettingsMenu::pomoPrevPage() {
    if (pomoSubPage > 0) {
        pomoSubPage--;
        Serial.printf("Pomo sub-page: %d (%s)\n", pomoSubPage, pomoPageLabels[pomoSubPage]);
    }
}

void SettingsMenu::openSettingsSubMenu() {
    settingsSubMenuOpen = true;
    settingsSubPage = SETTINGS_PAGE_VOLUME;
    Serial.println("Settings sub-menu opened");
}

void SettingsMenu::closeSettingsSubMenu() {
    settingsSubMenuOpen = false;
    settingsSubPage = 0;
    Serial.println("Settings sub-menu closed");
}

void SettingsMenu::settingsNextPage() {
    if (settingsSubPage < SETTINGS_NUM_PAGES - 1) {
        settingsSubPage++;
        Serial.printf("Settings sub-page: %d (%s)\n", settingsSubPage, settingsPageLabels[settingsSubPage]);
    }
}

void SettingsMenu::settingsPrevPage() {
    if (settingsSubPage > 0) {
        settingsSubPage--;
        Serial.printf("Settings sub-page: %d (%s)\n", settingsSubPage, settingsPageLabels[settingsSubPage]);
    }
}

bool SettingsMenu::handleTouch(bool touched, int16_t x, int16_t y) {
    if (!menuOpen) {
        wasTouched = touched;
        return false;
    }

    // Delegate to sub-menus if open
    if (pomoSubMenuOpen) {
        return handlePomoSubMenuTouch(touched, x, y);
    }
    if (mindfulSubMenuOpen) {
        return handleMindfulSubMenuTouch(touched, x, y);
    }
    if (settingsSubMenuOpen) {
        return handleSettingsSubMenuTouch(touched, x, y);
    }

    // With 90° CCW rotation:
    // - Visual vertical (swipe up/down) = raw X movement
    // - Visual horizontal (slider) = raw Y movement
    // Visual right = raw Y decreasing (inverted)
    // Visual down = raw X increasing

    static int16_t lastX = 0, lastY = 0;  // Store last valid position

    if (touched && !wasTouched) {
        // Touch started
        touchStartX = x;
        touchStartY = y;
        lastX = x;
        lastY = y;
        isDraggingSlider = false;
        isSwiping = false;
        Serial.printf("Touch start: raw(%d, %d)\n", x, y);
    } else if (touched && wasTouched) {
        // Touch continuing - store last position
        lastX = x;
        lastY = y;

        int16_t deltaX = x - touchStartX;  // For vertical swipe detection
        int16_t deltaY = y - touchStartY;  // For horizontal slider

        if (!isDraggingSlider && !isSwiping) {
            // Detect gesture type - deltaX is vertical, deltaY is horizontal (rotated!)
            if (abs(deltaX) > abs(deltaY) && abs(deltaX) > 30) {
                isSwiping = true;  // Vertical swipe for page change
                Serial.println("Swiping detected (vertical)");
            } else if (abs(deltaY) > 20) {
                isDraggingSlider = true;  // Horizontal drag for slider
                Serial.println("Slider drag detected");
            }
        }

        // No slider handling in main menu (sliders are in sub-menus)
    } else if (!touched && wasTouched) {
        // Touch ended - use last valid position
        int16_t deltaX = lastX - touchStartX;
        Serial.printf("Touch end: lastX=%d, startX=%d, deltaX=%d\n", lastX, touchStartX, deltaX);

        if (isSwiping) {
            // Swipe up (raw X decreases) = next page, swipe down (raw X increases) = prev page
            if (deltaX > SWIPE_THRESHOLD) {
                Serial.printf("Swipe down -> prev page (was %d)\n", currentPage);
                prevPage();
                Serial.printf("Now on page %d\n", currentPage);
            } else if (deltaX < -SWIPE_THRESHOLD) {
                Serial.printf("Swipe up -> next page (was %d)\n", currentPage);
                nextPage();
                Serial.printf("Now on page %d\n", currentPage);
            } else {
                Serial.printf("Swipe too short: %d (threshold=%d)\n", deltaX, SWIPE_THRESHOLD);
            }
        } else if (!isDraggingSlider && !isSwiping) {
            // Tap handling for main menu
            if (currentPage == PAGE_POMODORO) {
                // Tap to open pomodoro sub-menu
                openPomoSubMenu();
            } else if (currentPage == PAGE_MINDFULNESS) {
                // Tap to open mindfulness sub-menu
                openMindfulSubMenu();
            } else if (currentPage == PAGE_SETTINGS) {
                // Tap to open settings sub-menu
                openSettingsSubMenu();
            } else if (currentPage == PAGE_EXIT) {
                // Tap to close menu
                close();
            }
        }

        isDraggingSlider = false;
        isSwiping = false;
    }

    wasTouched = touched;
    return true;
}

void SettingsMenu::render(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight,
                          int16_t bufScreenX, int16_t bufScreenY, float micLevel) {
    if (!menuOpen) return;

    // Fill entire buffer with background
    for (int i = 0; i < bufWidth * bufHeight; i++) {
        buffer[i] = BG_COLOR;
    }

    // Delegate to sub-menus if open
    if (pomoSubMenuOpen) {
        renderPomoSubMenu(buffer, bufWidth, bufHeight);
        return;
    }
    if (mindfulSubMenuOpen) {
        renderMindfulSubMenu(buffer, bufWidth, bufHeight);
        return;
    }
    if (settingsSubMenuOpen) {
        renderSettingsSubMenu(buffer, bufWidth, bufHeight, micLevel);
        return;
    }

    // Layout for landscape screen - main menu
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, 25, mainPageLabels[currentPage], TEXT_COLOR);

    if (currentPage == PAGE_POMODORO) {
        // Pomodoro main entry page - shows status and opens sub-menu on tap
        if (pomodoroTimer == nullptr) {
            drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2, "NOT INIT", TEXT_COLOR);
        } else if (pomodoroTimer->isActive()) {
            // Show brief timer status when running
            uint32_t remaining = pomodoroTimer->getRemainingSeconds();
            int mins = remaining / 60;
            int secs = remaining % 60;
            char timeStr[16];
            sprintf(timeStr, "%02d:%02d", mins, secs);
            drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 - 20, timeStr, SLIDER_FILL_COLOR);

            const char* stateLabel = "WORKING";
            PomodoroState state = pomodoroTimer->getState();
            if (state == PomodoroState::ShortBreak) stateLabel = "SHORT BREAK";
            else if (state == PomodoroState::LongBreak) stateLabel = "LONG BREAK";
            else if (state == PomodoroState::Celebration) stateLabel = "DONE";
            else if (state == PomodoroState::WaitingForTap) stateLabel = "PAUSED";
            drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 + 20, stateLabel, TEXT_COLOR);

            drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H - 50, "TAP TO OPEN", ARROW_COLOR);
        } else {
            // Idle - show settings summary
            char durStr[32];
            sprintf(durStr, "%d MIN WORK", pomodoroTimer->getWorkMinutes());
            drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 - 20, durStr, TEXT_COLOR);

            drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 + 20, "TAP TO OPEN", ARROW_COLOR);
        }
    } else if (currentPage == PAGE_MINDFULNESS) {
        // Mindfulness entry page - shows breathing status
        if (breathingExercise == nullptr) {
            drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2, "NOT INIT", TEXT_COLOR);
        } else {
            // Show schedule status
            if (breathingExercise->isEnabled()) {
                char intervalStr[32];
                sprintf(intervalStr, "EVERY %d MIN", breathingExercise->getIntervalMinutes());
                drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 - 20, intervalStr, SLIDER_FILL_COLOR);
                drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 + 20, "SCHEDULED", TEXT_COLOR);
            } else {
                drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2, "SCHEDULE OFF", TEXT_COLOR);
            }
            drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H - 50, "TAP TO OPEN", ARROW_COLOR);
        }
    } else if (currentPage == PAGE_SETTINGS) {
        // Settings entry page - tap to open sub-menu
        drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 - 20, "VOLUME BRIGHT", TEXT_COLOR);
        drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 + 10, "MIC COLOR TIME", TEXT_COLOR);
        drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H - 50, "TAP TO OPEN", ARROW_COLOR);
    } else if (currentPage == PAGE_EXIT) {
        // Exit page - tap to close menu
        drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 - 15, "TAP TO", TEXT_COLOR);
        drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 + 15, "CLOSE", TEXT_COLOR);
    }

    // Page pips - vertical on right side
    int16_t pipX = SCREEN_W - 15;
    int16_t pipSpacing = 30;
    int16_t pipsStartY = SCREEN_H / 2 - (NUM_MAIN_PAGES - 1) * pipSpacing / 2;
    for (int i = 0; i < NUM_MAIN_PAGES; i++) {
        int16_t pipY = pipsStartY + i * pipSpacing;
        if (i == currentPage) {
            // Current page: larger bright pip
            drawFilledRect(buffer, bufWidth, bufHeight, pipX - 5, pipY - 5, 10, 10, TEXT_COLOR);
        } else {
            // Other pages: small dim pip
            drawFilledRect(buffer, bufWidth, bufHeight, pipX - 3, pipY - 3, 6, 6, ARROW_COLOR);
        }
    }
}

void SettingsMenu::updateSliderFromTouch(int16_t y, int16_t bufHeight) {}

void SettingsMenu::saveSettings() {
    prefs.begin("robot", false);
    prefs.putInt("volume", values[0]);
    prefs.putInt("brightness", values[1]);
    prefs.putInt("micSens", values[2]);
    prefs.putInt("micThr", values[3]);
    prefs.putInt("colorIdx", colorIndex);
    prefs.putBool("is24Hour", is24Hour);
    prefs.putChar("gmtOffset", gmtOffsetHours);
    prefs.putBool("wifiOn", wifiEnabled);
    prefs.putBool("offlineCfg", offlineModeConfigured);
    prefs.end();
    settingsVersion++;  // Increment version for web sync detection
    Serial.printf("Settings saved (v%u): Vol=%d, Brt=%d, TZ=%+d, WiFi=%s\n",
                  settingsVersion, values[0], values[1], gmtOffsetHours,
                  wifiEnabled ? "ON" : "OFF");
}

void SettingsMenu::loadSettings() {
    prefs.begin("robot", true);
    values[0] = prefs.getInt("volume", 80);
    values[1] = prefs.getInt("brightness", 100);
    values[2] = prefs.getInt("micSens", 50);
    values[3] = prefs.getInt("micThr", 50);
    colorIndex = constrain(prefs.getInt("colorIdx", 0), 0, NUM_COLOR_PRESETS - 1);
    is24Hour = prefs.getBool("is24Hour", false);
    gmtOffsetHours = prefs.getChar("gmtOffset", 0);  // Default: UTC
    wifiEnabled = prefs.getBool("wifiOn", true);  // Default: WiFi enabled
    offlineModeConfigured = prefs.getBool("offlineCfg", false);  // Default: not configured
    prefs.end();
    Serial.printf("Settings loaded: Vol=%d, Brt=%d, WiFi=%s, Offline=%s\n",
                  values[0], values[1],
                  wifiEnabled ? "ON" : "OFF", offlineModeConfigured ? "YES" : "NO");
}

void SettingsMenu::setVolume(int val) {
    values[0] = constrain(val, 0, 100);
    saveSettings();
}

void SettingsMenu::setBrightness(int val) {
    values[1] = constrain(val, 0, 100);
    saveSettings();
}

void SettingsMenu::setMicSensitivity(int val) {
    values[2] = constrain(val, 0, 100);
    saveSettings();
}

void SettingsMenu::setMicThreshold(int val) {
    values[3] = constrain(val, 0, 100);
    saveSettings();
}

void SettingsMenu::setTime(int hour, int minute) {
    timeHour = constrain(hour, 0, 23);
    timeMinute = constrain(minute, 0, 59);
    saveSettings();
}

void SettingsMenu::setTimeFormat(bool use24Hour) {
    is24Hour = use24Hour;
    saveSettings();
}

void SettingsMenu::setColorIndex(int index) {
    colorIndex = constrain(index, 0, NUM_COLOR_PRESETS - 1);
    saveSettings();
}

void SettingsMenu::setWiFiEnabled(bool enabled) {
    wifiEnabled = enabled;
    saveSettings();
    Serial.printf("WiFi %s\n", enabled ? "enabled" : "disabled");
}

void SettingsMenu::setOfflineModeConfigured(bool configured) {
    offlineModeConfigured = configured;
    saveSettings();
    Serial.printf("Offline mode %s\n", configured ? "configured" : "cleared");
}

void SettingsMenu::setGmtOffsetHours(int8_t hours) {
    gmtOffsetHours = constrain(hours, -12, 14);
    saveSettings();
    Serial.printf("Timezone set to UTC%+d\n", gmtOffsetHours);
}

uint16_t SettingsMenu::getColorRGB565() const {
    return COLOR_PRESETS[colorIndex];
}

int SettingsMenu::getTimeHour() const {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {  // 0 = don't wait
        return timeinfo.tm_hour;
    }
    return timeHour;  // Fallback to internal time
}

int SettingsMenu::getTimeMinute() const {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        return timeinfo.tm_min;
    }
    return timeMinute;  // Fallback to internal time
}

void SettingsMenu::drawFilledRect(uint16_t* buffer, int16_t bufW, int16_t bufH,
                                  int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    // 90° CCW rotation: screen (sx, sy) → buffer (sy, bufH - 1 - sx)
    for (int16_t sy = y; sy < y + h; sy++) {
        for (int16_t sx = x; sx < x + w; sx++) {
            int16_t bx = sy;
            int16_t by = bufH - 1 - sx;
            if (bx >= 0 && bx < bufW && by >= 0 && by < bufH) {
                buffer[by * bufW + bx] = color;
            }
        }
    }
}

void SettingsMenu::drawCenteredText(uint16_t* buffer, int16_t bufW, int16_t bufH,
                                    int16_t centerX, int16_t y, const char* text, uint16_t color) {
    int len = strlen(text);
    int16_t textWidth = len * 18;
    int16_t x = centerX - textWidth / 2;
    drawText(buffer, bufW, bufH, x, y, text, color);
}

void SettingsMenu::drawText(uint16_t* buffer, int16_t bufW, int16_t bufH,
                            int16_t x, int16_t y, const char* text, uint16_t color) {
    int16_t curX = x;
    while (*text) {
        drawChar(buffer, bufW, bufH, curX, y, *text, color);
        curX += 18;
        text++;
    }
}

void SettingsMenu::drawChar(uint16_t* buffer, int16_t bufW, int16_t bufH,
                            int16_t x, int16_t y, char c, uint16_t color) {
    int fontIdx = -1;

    if (c >= '0' && c <= '9') {
        fontIdx = c - '0';
    } else if (c == ' ') {
        fontIdx = 10;
    } else if (c >= 'A' && c <= 'Z') {
        fontIdx = 11 + (c - 'A');
    } else if (c >= 'a' && c <= 'z') {
        fontIdx = 11 + (c - 'a');
    } else if (c == '^') {
        fontIdx = 38;
    } else if (c == 'v') {
        fontIdx = 39;
    } else if (c == '-') {
        fontIdx = 40;
    }

    if (fontIdx < 0 || fontIdx >= 41) return;

    const uint8_t* charData = FONT_5X7[fontIdx];
    // 3x scaling with 90° CCW rotation
    for (int col = 0; col < 5; col++) {
        uint8_t colBits = charData[col];
        for (int row = 0; row < 7; row++) {
            if (colBits & (1 << row)) {
                for (int sy = 0; sy < 3; sy++) {
                    for (int sx = 0; sx < 3; sx++) {
                        int16_t screenX = x + col * 3 + sx;
                        int16_t screenY = y + row * 3 + sy;
                        // 90° CCW: buffer (sy, bufH - 1 - sx)
                        int16_t bx = screenY;
                        int16_t by = bufH - 1 - screenX;
                        if (bx >= 0 && bx < bufW && by >= 0 && by < bufH) {
                            buffer[by * bufW + bx] = color;
                        }
                    }
                }
            }
        }
    }
}

void SettingsMenu::drawLargeDigit(uint16_t* buffer, int16_t bufW, int16_t bufH,
                                   int16_t x, int16_t y, int digit, uint16_t color, int scale) {
    if (digit < 0 || digit > 9) return;

    const uint8_t* charData = FONT_5X7[digit];
    for (int col = 0; col < 5; col++) {
        uint8_t colBits = charData[col];
        for (int row = 0; row < 7; row++) {
            if (colBits & (1 << row)) {
                for (int sy = 0; sy < scale; sy++) {
                    for (int sx = 0; sx < scale; sx++) {
                        int16_t screenX = x + col * scale + sx;
                        int16_t screenY = y + row * scale + sy;
                        // 90° CCW: buffer (sy, bufH - 1 - sx)
                        int16_t bx = screenY;
                        int16_t by = bufH - 1 - screenX;
                        if (bx >= 0 && bx < bufW && by >= 0 && by < bufH) {
                            buffer[by * bufW + bx] = color;
                        }
                    }
                }
            }
        }
    }
}

void SettingsMenu::drawTimeDisplay(uint16_t* buffer, int16_t bufW, int16_t bufH) {
    // Large HH:MM display
    // Each digit is 5*6=30 pixels wide, 7*6=42 pixels tall at scale 6
    const int digitScale = 6;
    const int digitW = 5 * digitScale;
    const int digitH = 7 * digitScale;
    const int colonW = 5 * digitScale;
    const int spacing = 8;

    // Total width: 4 digits + colon + spacing
    int totalW = 4 * digitW + colonW + 4 * spacing;
    int startX = (SCREEN_W - totalW) / 2;
    int digitY = SCREEN_H / 2 - digitH / 2 - 10;

    // Get digits - use getters to get NTP time if available
    int hour = getTimeHour();
    int minute = getTimeMinute();
    int d0 = hour / 10;
    int d1 = hour % 10;
    int d2 = minute / 10;
    int d3 = minute % 10;

    // Draw all digits in cyan (the eye color)
    int16_t xPos = startX;

    // Hour tens
    drawLargeDigit(buffer, bufW, bufH, xPos, digitY, d0, SLIDER_FILL_COLOR, digitScale);
    xPos += digitW + spacing;

    // Hour ones
    drawLargeDigit(buffer, bufW, bufH, xPos, digitY, d1, SLIDER_FILL_COLOR, digitScale);
    xPos += digitW + spacing;

    // Colon
    drawCenteredText(buffer, bufW, bufH, xPos + colonW / 2, digitY + digitH / 3, ":", TEXT_COLOR);
    xPos += colonW + spacing;

    // Minute tens
    drawLargeDigit(buffer, bufW, bufH, xPos, digitY, d2, SLIDER_FILL_COLOR, digitScale);
    xPos += digitW + spacing;

    // Minute ones
    drawLargeDigit(buffer, bufW, bufH, xPos, digitY, d3, SLIDER_FILL_COLOR, digitScale);

    // Show AM/PM for 12-hour mode
    if (!is24Hour) {
        const char* ampm = (hour >= 12) ? "PM" : "AM";
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, digitY + digitH + 20, ampm, ARROW_COLOR);
    }
}

void SettingsMenu::addMinutes(int minutes) {
    // Convert current time to total minutes
    int totalMinutes = timeHour * 60 + timeMinute;

    // Add minutes with wraparound (24 hours = 1440 minutes)
    totalMinutes += minutes;
    while (totalMinutes < 0) totalMinutes += 1440;
    while (totalMinutes >= 1440) totalMinutes -= 1440;

    // Convert back to hours and minutes
    timeHour = totalMinutes / 60;
    timeMinute = totalMinutes % 60;

    Serial.printf("Time: %02d:%02d\n", timeHour, timeMinute);
}

bool SettingsMenu::handlePomoSubMenuTouch(bool touched, int16_t x, int16_t y) {
    static int16_t lastX = 0, lastY = 0;

    if (touched && !wasTouched) {
        // Touch started
        touchStartX = x;
        touchStartY = y;
        lastX = x;
        lastY = y;
        isDraggingSlider = false;
        isSwiping = false;
    } else if (touched && wasTouched) {
        // Touch continuing
        lastX = x;
        lastY = y;

        int16_t deltaX = x - touchStartX;
        int16_t deltaY = y - touchStartY;

        if (!isDraggingSlider && !isSwiping) {
            if (abs(deltaX) > abs(deltaY) && abs(deltaX) > 30) {
                isSwiping = true;
            } else if (abs(deltaY) > 20) {
                isDraggingSlider = true;
            }
        }

        // Handle slider drag for duration pages
        if (isDraggingSlider && pomodoroTimer != nullptr) {
            int newValue;
            switch (pomoSubPage) {
                case POMO_PAGE_WORK:
                    newValue = ((300 - y) * 59) / 250 + 1;  // 1-60 min
                    newValue = constrain(newValue, 1, 60);
                    pomodoroTimer->setWorkMinutes(newValue);
                    break;
                case POMO_PAGE_SHORT_BREAK:
                    newValue = ((300 - y) * 29) / 250 + 1;  // 1-30 min
                    newValue = constrain(newValue, 1, 30);
                    pomodoroTimer->setShortBreakMinutes(newValue);
                    break;
                case POMO_PAGE_LONG_BREAK:
                    newValue = ((300 - y) * 59) / 250 + 1;  // 1-60 min
                    newValue = constrain(newValue, 1, 60);
                    pomodoroTimer->setLongBreakMinutes(newValue);
                    break;
                case POMO_PAGE_SESSIONS:
                    newValue = ((300 - y) * 7) / 250 + 1;  // 1-8 sessions
                    newValue = constrain(newValue, 1, 8);
                    pomodoroTimer->setSessionsBeforeLongBreak(newValue);
                    break;
            }
        }
    } else if (!touched && wasTouched) {
        // Touch ended
        int16_t deltaX = lastX - touchStartX;

        if (isSwiping) {
            if (deltaX > SWIPE_THRESHOLD) {
                pomoPrevPage();
            } else if (deltaX < -SWIPE_THRESHOLD) {
                pomoNextPage();
            }
        } else if (!isDraggingSlider && !isSwiping) {
            // Tap handling
            if (pomoSubPage == POMO_PAGE_START_STOP) {
                // Start or stop timer
                if (pomodoroTimer != nullptr) {
                    if (pomodoroTimer->isActive()) {
                        pomodoroTimer->stop();
                        Serial.println("Pomodoro stopped");
                    } else {
                        pomodoroTimer->start();
                        Serial.println("Pomodoro started");
                        // Close all menus and return to main screen
                        close();
                    }
                }
            } else if (pomoSubPage == POMO_PAGE_TICKING) {
                // Toggle ticking
                if (pomodoroTimer != nullptr) {
                    pomodoroTimer->setTickingEnabled(!pomodoroTimer->isTickingEnabled());
                    Serial.printf("Ticking: %s\n", pomodoroTimer->isTickingEnabled() ? "ON" : "OFF");
                }
            } else if (pomoSubPage == POMO_PAGE_BACK) {
                closePomoSubMenu();
            }
        }

        isDraggingSlider = false;
        isSwiping = false;
    }

    wasTouched = touched;
    return true;
}

void SettingsMenu::renderPomoSubMenu(uint16_t* buffer, int16_t bufW, int16_t bufH) {
    // Title - show "START" or "STOP" for first page based on timer state
    const char* pageTitle = pomoPageLabels[pomoSubPage];
    if (pomoSubPage == POMO_PAGE_START_STOP && pomodoroTimer != nullptr && pomodoroTimer->isActive()) {
        pageTitle = "STOP";
    }
    drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, 25, pageTitle, TEXT_COLOR);

    if (pomoSubPage == POMO_PAGE_START_STOP) {
        // Start/Stop page
        if (pomodoroTimer == nullptr) {
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2, "NOT INIT", TEXT_COLOR);
        } else if (pomodoroTimer->isActive()) {
            // Show current status when running
            uint32_t remaining = pomodoroTimer->getRemainingSeconds();
            int mins = remaining / 60;
            int secs = remaining % 60;
            char timeStr[16];
            sprintf(timeStr, "%02d:%02d", mins, secs);
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 - 30, timeStr, SLIDER_FILL_COLOR);

            const char* stateLabel = "WORKING";
            PomodoroState state = pomodoroTimer->getState();
            if (state == PomodoroState::ShortBreak) stateLabel = "SHORT BREAK";
            else if (state == PomodoroState::LongBreak) stateLabel = "LONG BREAK";
            else if (state == PomodoroState::Celebration) stateLabel = "COMPLETE";
            else if (state == PomodoroState::WaitingForTap) stateLabel = "TAP NEXT";
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 20, stateLabel, TEXT_COLOR);

            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 50, "TAP TO STOP", ARROW_COLOR);
        } else {
            // Show start prompt when idle
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 - 20, "TAP TO", TEXT_COLOR);
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 10, "START", TEXT_COLOR);

            char durStr[16];
            sprintf(durStr, "%d MIN", pomodoroTimer->getWorkMinutes());
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 50, durStr, ARROW_COLOR);
        }
    } else if (pomoSubPage == POMO_PAGE_WORK || pomoSubPage == POMO_PAGE_SHORT_BREAK ||
               pomoSubPage == POMO_PAGE_LONG_BREAK || pomoSubPage == POMO_PAGE_SESSIONS) {
        // Duration/count slider pages
        int currentValue = 0;
        int maxValue = 60;
        const char* unit = "MIN";

        if (pomodoroTimer != nullptr) {
            switch (pomoSubPage) {
                case POMO_PAGE_WORK:
                    currentValue = pomodoroTimer->getWorkMinutes();
                    maxValue = 60;
                    unit = "MIN";
                    break;
                case POMO_PAGE_SHORT_BREAK:
                    currentValue = pomodoroTimer->getShortBreakMinutes();
                    maxValue = 30;
                    unit = "MIN";
                    break;
                case POMO_PAGE_LONG_BREAK:
                    currentValue = pomodoroTimer->getLongBreakMinutes();
                    maxValue = 60;
                    unit = "MIN";
                    break;
                case POMO_PAGE_SESSIONS:
                    currentValue = pomodoroTimer->getSessionsBeforeLongBreak();
                    maxValue = 8;
                    unit = "";
                    break;
            }
        }

        // Draw horizontal slider
        int16_t sliderX = 50;
        int16_t sliderW = SCREEN_W - 100;
        int16_t sliderY = SCREEN_H / 2 - 15;
        int16_t sliderH = 30;

        drawFilledRect(buffer, bufW, bufH, sliderX, sliderY, sliderW, sliderH, SLIDER_BG_COLOR);

        int fillPercent = (currentValue * 100) / maxValue;
        int16_t fillW = (sliderW * fillPercent) / 100;
        drawFilledRect(buffer, bufW, bufH, sliderX, sliderY, fillW, sliderH, SLIDER_FILL_COLOR);

        // Knob
        int16_t knobW = 24;
        int16_t knobH = 50;
        int16_t knobX = sliderX + fillW - knobW / 2;
        knobX = constrain(knobX, sliderX - knobW/2, sliderX + sliderW - knobW/2);
        int16_t knobY = sliderY - 10;
        drawFilledRect(buffer, bufW, bufH, knobX, knobY, knobW, knobH, KNOB_COLOR);

        // Value display
        char valStr[16];
        sprintf(valStr, "%d %s", currentValue, unit);
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 50, valStr, TEXT_COLOR);
    } else if (pomoSubPage == POMO_PAGE_TICKING) {
        // Ticking toggle
        bool tickEnabled = pomodoroTimer != nullptr && pomodoroTimer->isTickingEnabled();
        const char* tickStr = tickEnabled ? "ON" : "OFF";
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 - 10, tickStr, SLIDER_FILL_COLOR);
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 50, "TAP TO TOGGLE", ARROW_COLOR);
    } else if (pomoSubPage == POMO_PAGE_BACK) {
        // Back page
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 - 15, "TAP TO", TEXT_COLOR);
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 15, "GO BACK", TEXT_COLOR);
    }

    // Page pips for sub-menu
    int16_t pipX = SCREEN_W - 15;
    int16_t pipSpacing = 22;
    int16_t pipsStartY = SCREEN_H / 2 - (POMO_NUM_PAGES - 1) * pipSpacing / 2;
    for (int i = 0; i < POMO_NUM_PAGES; i++) {
        int16_t pipY = pipsStartY + i * pipSpacing;
        if (i == pomoSubPage) {
            drawFilledRect(buffer, bufW, bufH, pipX - 5, pipY - 5, 10, 10, TEXT_COLOR);
        } else {
            drawFilledRect(buffer, bufW, bufH, pipX - 3, pipY - 3, 6, 6, ARROW_COLOR);
        }
    }
}

bool SettingsMenu::handleSettingsSubMenuTouch(bool touched, int16_t x, int16_t y) {
    static int16_t lastX = 0, lastY = 0;

    if (touched && !wasTouched) {
        // Touch started
        touchStartX = x;
        touchStartY = y;
        lastX = x;
        lastY = y;
        isDraggingSlider = false;
        isSwiping = false;
    } else if (touched && wasTouched) {
        // Touch continuing
        lastX = x;
        lastY = y;

        int16_t deltaX = x - touchStartX;
        int16_t deltaY = y - touchStartY;

        if (!isDraggingSlider && !isSwiping) {
            if (abs(deltaX) > abs(deltaY) && abs(deltaX) > 30) {
                isSwiping = true;
            } else if (abs(deltaY) > 20) {
                isDraggingSlider = true;
            }
        }

        // Handle slider drag for value pages
        if (isDraggingSlider) {
            int newValue;
            switch (settingsSubPage) {
                case SETTINGS_PAGE_VOLUME:
                    newValue = ((300 - y) * 100) / 250;
                    values[0] = constrain(newValue, 0, 100);
                    break;
                case SETTINGS_PAGE_BRIGHTNESS:
                    newValue = ((300 - y) * 100) / 250;
                    values[1] = constrain(newValue, 0, 100);
                    break;
                case SETTINGS_PAGE_MIC_GAIN:
                    newValue = ((300 - y) * 100) / 250;
                    values[2] = constrain(newValue, 0, 100);
                    break;
                case SETTINGS_PAGE_MIC_THRESHOLD:
                    newValue = ((300 - y) * 100) / 250;
                    values[3] = constrain(newValue, 0, 100);
                    break;
            }
        }
    } else if (!touched && wasTouched) {
        // Touch ended
        int16_t deltaX = lastX - touchStartX;
        int16_t deltaY = lastY - touchStartY;

        if (isSwiping) {
            if (deltaX > SWIPE_THRESHOLD) {
                settingsPrevPage();
            } else if (deltaX < -SWIPE_THRESHOLD) {
                settingsNextPage();
            }
        } else if (isDraggingSlider && settingsSubPage == SETTINGS_PAGE_COLOR) {
            // Horizontal drag on color page: cycle preset
            if (deltaY < -30) {
                colorIndex = (colorIndex + 1) % NUM_COLOR_PRESETS;
                Serial.printf("Color: %s (%d)\n", COLOR_PRESET_NAMES[colorIndex], colorIndex);
            } else if (deltaY > 30) {
                colorIndex = (colorIndex + NUM_COLOR_PRESETS - 1) % NUM_COLOR_PRESETS;
                Serial.printf("Color: %s (%d)\n", COLOR_PRESET_NAMES[colorIndex], colorIndex);
            }
        } else if (isDraggingSlider && settingsSubPage == SETTINGS_PAGE_TIME) {
            // Horizontal drag on time page
            int minutes = 0;
            int absDelta = abs(deltaY);
            if (absDelta > 150) minutes = 60;
            else if (absDelta > 100) minutes = 30;
            else if (absDelta > 60) minutes = 15;
            else if (absDelta > 30) minutes = 5;
            else if (absDelta > 15) minutes = 1;

            if (minutes > 0) {
                if (deltaY < 0) addMinutes(minutes);
                else addMinutes(-minutes);
            }
        } else if (isDraggingSlider && settingsSubPage == SETTINGS_PAGE_TIMEZONE) {
            // Horizontal drag on timezone page
            int hours = 0;
            int absDelta = abs(deltaY);
            if (absDelta > 100) hours = 3;
            else if (absDelta > 50) hours = 2;
            else if (absDelta > 25) hours = 1;

            if (hours > 0) {
                int newOffset = gmtOffsetHours + (deltaY < 0 ? hours : -hours);
                gmtOffsetHours = constrain(newOffset, -12, 14);
                Serial.printf("Timezone: UTC%+d\n", gmtOffsetHours);
            }
        } else if (!isDraggingSlider && !isSwiping) {
            // Tap handling
            if (settingsSubPage == SETTINGS_PAGE_TIME) {
                // Tap left half = increment hour, tap right half = increment minute
                if (lastY > 175) {
                    timeHour = (timeHour + 1) % 24;
                    Serial.printf("Time: %02d:%02d (hour++)\n", timeHour, timeMinute);
                } else {
                    timeMinute = (timeMinute + 1) % 60;
                    Serial.printf("Time: %02d:%02d (min++)\n", timeHour, timeMinute);
                }
            } else if (settingsSubPage == SETTINGS_PAGE_TIME_FORMAT) {
                is24Hour = !is24Hour;
                Serial.printf("Time format: %s\n", is24Hour ? "24H" : "12H");
            } else if (settingsSubPage == SETTINGS_PAGE_TIMEZONE) {
                // Tap left half = decrement, tap right half = increment
                if (lastY > SCREEN_W / 2) {
                    gmtOffsetHours = constrain(gmtOffsetHours + 1, -12, 14);
                } else {
                    gmtOffsetHours = constrain(gmtOffsetHours - 1, -12, 14);
                }
                Serial.printf("Timezone: UTC%+d\n", gmtOffsetHours);
                saveSettings();
            } else if (settingsSubPage == SETTINGS_PAGE_WIFI) {
                wifiEnabled = !wifiEnabled;
                Serial.printf("WiFi: %s\n", wifiEnabled ? "ON" : "OFF");
                saveSettings();
            } else if (settingsSubPage == SETTINGS_PAGE_BACK) {
                closeSettingsSubMenu();
            }
        }

        isDraggingSlider = false;
        isSwiping = false;
    }

    wasTouched = touched;
    return true;
}

void SettingsMenu::renderSettingsSubMenu(uint16_t* buffer, int16_t bufW, int16_t bufH, float micLevel) {
    // Title
    drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, 25, settingsPageLabels[settingsSubPage], TEXT_COLOR);

    if (settingsSubPage >= SETTINGS_PAGE_VOLUME && settingsSubPage <= SETTINGS_PAGE_MIC_THRESHOLD) {
        // Horizontal slider pages
        int sliderIdx = settingsSubPage;  // 0-3 maps to values[0-3]

        int16_t sliderX = 50;
        int16_t sliderW = SCREEN_W - 100;
        int16_t sliderY = SCREEN_H / 2 - 15;
        int16_t sliderH = 30;

        drawFilledRect(buffer, bufW, bufH, sliderX, sliderY, sliderW, sliderH, SLIDER_BG_COLOR);

        int16_t fillW = (sliderW * values[sliderIdx]) / 100;
        drawFilledRect(buffer, bufW, bufH, sliderX, sliderY, fillW, sliderH, SLIDER_FILL_COLOR);

        // Center marker for mic gain (0dB position)
        if (settingsSubPage == SETTINGS_PAGE_MIC_GAIN) {
            int16_t centerX = sliderX + sliderW / 2;
            int16_t markerW = 3;
            int16_t markerH = sliderH + 20;
            int16_t markerY = sliderY - 10;
            drawFilledRect(buffer, bufW, bufH, centerX - markerW / 2, markerY, markerW, markerH, TEXT_COLOR);
        }

        // Knob
        int16_t knobW = 24;
        int16_t knobH = 50;
        int16_t knobX = sliderX + fillW - knobW / 2;
        knobX = constrain(knobX, sliderX - knobW/2, sliderX + sliderW - knobW/2);
        int16_t knobY = sliderY - 10;
        drawFilledRect(buffer, bufW, bufH, knobX, knobY, knobW, knobH, KNOB_COLOR);

        // Value display
        char valStr[16];
        if (settingsSubPage == SETTINGS_PAGE_MIC_GAIN) {
            int slider = values[sliderIdx];
            if (slider < 50) {
                float t = slider / 50.0f;
                float attenuation = 0.0625f + t * (1.0f - 0.0625f);
                float attenDb = 20.0f * log10f(attenuation);
                sprintf(valStr, "%.0f DB", attenDb);
            } else {
                int gainRange = slider - 50;
                int gainDb = (gainRange < 7) ? 0 : (gainRange < 14) ? 6 : (gainRange < 21) ? 12 :
                            (gainRange < 28) ? 18 : (gainRange < 35) ? 24 : (gainRange < 42) ? 30 :
                            (gainRange < 49) ? 36 : 42;
                sprintf(valStr, "+%d DB", gainDb);
            }
        } else {
            sprintf(valStr, "%d", values[sliderIdx]);
        }
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 50, valStr, TEXT_COLOR);

        // Show mic level on threshold page
        if (settingsSubPage == SETTINGS_PAGE_MIC_THRESHOLD) {
            char micStr[16];
            sprintf(micStr, "LEVEL %d", (int)(micLevel * 100));
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, 60, micStr, TEXT_COLOR);

            int16_t levelBarX = 50;
            int16_t levelBarW = SCREEN_W - 100;
            int16_t levelBarY = 80;
            int16_t levelBarH = 10;

            drawFilledRect(buffer, bufW, bufH, levelBarX, levelBarY, levelBarW, levelBarH, SLIDER_BG_COLOR);

            int16_t levelFillW = (int16_t)(levelBarW * micLevel);
            if (levelFillW > 0) {
                drawFilledRect(buffer, bufW, bufH, levelBarX, levelBarY, levelFillW, levelBarH,
                               micLevel > (values[3] / 100.0f) ? 0xF800 : SLIDER_FILL_COLOR);
            }
        }
    } else if (settingsSubPage == SETTINGS_PAGE_COLOR) {
        // Eye mockup with selected color
        uint16_t eyeCol = COLOR_PRESETS[colorIndex];
        int16_t eyeW = 60;
        int16_t eyeH = 80;
        int16_t eyeSpacing = 50;
        int16_t eyeCenterY = SCREEN_H / 3;
        int16_t leftEyeX = SCREEN_W / 2 - eyeSpacing / 2 - eyeW;
        int16_t rightEyeX = SCREEN_W / 2 + eyeSpacing / 2;
        int16_t eyeY = eyeCenterY - eyeH / 2;

        drawFilledRect(buffer, bufW, bufH, leftEyeX, eyeY, eyeW, eyeH, eyeCol);
        drawFilledRect(buffer, bufW, bufH, rightEyeX, eyeY, eyeW, eyeH, eyeCol);

        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 30, COLOR_PRESET_NAMES[colorIndex], TEXT_COLOR);
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 80, "SWIPE LR", ARROW_COLOR);
    } else if (settingsSubPage == SETTINGS_PAGE_TIME) {
        drawTimeDisplay(buffer, bufW, bufH);
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 40, "TAP TO SET", ARROW_COLOR);
    } else if (settingsSubPage == SETTINGS_PAGE_TIME_FORMAT) {
        const char* formatStr = is24Hour ? "24 HOUR" : "12 HOUR";
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 - 30, formatStr, SLIDER_FILL_COLOR);

        char exampleStr[16];
        int hour = getTimeHour();
        int minute = getTimeMinute();
        if (is24Hour) {
            sprintf(exampleStr, "%02d:%02d", hour, minute);
        } else {
            int displayHour = hour % 12;
            if (displayHour == 0) displayHour = 12;
            const char* ampm = (hour >= 12) ? "PM" : "AM";
            sprintf(exampleStr, "%d:%02d %s", displayHour, minute, ampm);
        }
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 20, exampleStr, TEXT_COLOR);
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 40, "TAP TO TOGGLE", ARROW_COLOR);
    } else if (settingsSubPage == SETTINGS_PAGE_TIMEZONE) {
        // Timezone offset display
        char tzStr[16];
        if (gmtOffsetHours >= 0) {
            sprintf(tzStr, "UTC+%d", gmtOffsetHours);
        } else {
            sprintf(tzStr, "UTC%d", gmtOffsetHours);
        }
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 - 30, tzStr, SLIDER_FILL_COLOR);
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 20, "FOR NTP SYNC", TEXT_COLOR);
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 40, "TAP +/- OR DRAG", ARROW_COLOR);
    } else if (settingsSubPage == SETTINGS_PAGE_WIFI) {
        // WiFi on/off toggle
        const char* wifiStatus = wifiEnabled ? "WIFI ON" : "WIFI OFF";
        uint16_t statusColor = wifiEnabled ? SLIDER_FILL_COLOR : ARROW_COLOR;
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 - 30, wifiStatus, statusColor);

        if (wifiEnabled) {
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 20, "AP OR NETWORK", TEXT_COLOR);
        } else {
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 20, "NO CONNECTION", TEXT_COLOR);
        }
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 40, "TAP TO TOGGLE", ARROW_COLOR);
    } else if (settingsSubPage == SETTINGS_PAGE_BACK) {
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 - 15, "TAP TO", TEXT_COLOR);
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 15, "GO BACK", TEXT_COLOR);
    }

    // Page pips for settings sub-menu
    int16_t pipX = SCREEN_W - 15;
    int16_t pipSpacing = 22;
    int16_t pipsStartY = SCREEN_H / 2 - (SETTINGS_NUM_PAGES - 1) * pipSpacing / 2;
    for (int i = 0; i < SETTINGS_NUM_PAGES; i++) {
        int16_t pipY = pipsStartY + i * pipSpacing;
        if (i == settingsSubPage) {
            drawFilledRect(buffer, bufW, bufH, pipX - 5, pipY - 5, 10, 10, TEXT_COLOR);
        } else {
            drawFilledRect(buffer, bufW, bufH, pipX - 3, pipY - 3, 6, 6, ARROW_COLOR);
        }
    }
}

void SettingsMenu::renderTimeOnly(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight, uint16_t color, bool showColon) {
    // Clear buffer to black
    for (int i = 0; i < bufWidth * bufHeight; i++) {
        buffer[i] = BG_COLOR;
    }

    // Draw LARGE time display (~75% of screen)
    // Scale 11: digit = 55x77, total width ~320px (74% of 435)
    const int digitScale = 11;
    const int digitW = 5 * digitScale;
    const int digitH = 7 * digitScale;
    const int colonW = 3 * digitScale;  // Narrower colon
    const int spacing = 12;

    // Total width: 4 digits + colon + spacing
    int totalW = 4 * digitW + colonW + 4 * spacing;
    int startX = (SCREEN_W - totalW) / 2;
    int digitY = SCREEN_H / 2 - digitH / 2;

    // Get digits - use getters to get NTP time if available
    int hour = getTimeHour();
    int minute = getTimeMinute();
    int d0 = hour / 10;
    int d1 = hour % 10;
    int d2 = minute / 10;
    int d3 = minute % 10;

    // Draw all digits in the eye color
    int16_t xPos = startX;

    // Hour tens
    drawLargeDigit(buffer, bufWidth, bufHeight, xPos, digitY, d0, color, digitScale);
    xPos += digitW + spacing;

    // Hour ones
    drawLargeDigit(buffer, bufWidth, bufHeight, xPos, digitY, d1, color, digitScale);
    xPos += digitW + spacing;

    // Colon - draw two squares (only if showColon is true for blinking effect)
    if (showColon) {
        int16_t colonX = xPos + colonW / 2;
        int16_t dotSize = digitScale;
        int16_t dotY1 = digitY + digitH / 3 - dotSize / 2;
        int16_t dotY2 = digitY + 2 * digitH / 3 - dotSize / 2;
        drawFilledRect(buffer, bufWidth, bufHeight, colonX - dotSize/2, dotY1, dotSize, dotSize, color);
        drawFilledRect(buffer, bufWidth, bufHeight, colonX - dotSize/2, dotY2, dotSize, dotSize, color);
    }
    xPos += colonW + spacing;

    // Minute tens
    drawLargeDigit(buffer, bufWidth, bufHeight, xPos, digitY, d2, color, digitScale);
    xPos += digitW + spacing;

    // Minute ones
    drawLargeDigit(buffer, bufWidth, bufHeight, xPos, digitY, d3, color, digitScale);
}

void SettingsMenu::renderCountdown(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight,
                                    int minutes, int seconds, uint16_t color, bool showColon,
                                    const char* label) {
    // Clear buffer to black
    for (int i = 0; i < bufWidth * bufHeight; i++) {
        buffer[i] = BG_COLOR;
    }

    // Draw LARGE countdown display (MM:SS format, ~75% of screen)
    // Scale 11: digit = 55x77, total width ~320px (74% of 435)
    const int digitScale = 11;
    const int digitW = 5 * digitScale;
    const int digitH = 7 * digitScale;
    const int colonW = 3 * digitScale;  // Narrower colon
    const int spacing = 12;

    // Total width: 4 digits + colon + spacing
    int totalW = 4 * digitW + colonW + 4 * spacing;
    int startX = (SCREEN_W - totalW) / 2;
    int digitY = SCREEN_H / 2 - digitH / 2;

    // Draw label above timer if provided
    if (label != nullptr) {
        // Position label above the timer digits (with some margin)
        int16_t labelY = digitY - 40;  // 40px above timer
        drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, labelY, label, color);
    }

    // Get digits (MM:SS format)
    int d0 = (minutes / 10) % 10;
    int d1 = minutes % 10;
    int d2 = (seconds / 10) % 10;
    int d3 = seconds % 10;

    // Draw all digits in the eye color
    int16_t xPos = startX;

    // Minute tens
    drawLargeDigit(buffer, bufWidth, bufHeight, xPos, digitY, d0, color, digitScale);
    xPos += digitW + spacing;

    // Minute ones
    drawLargeDigit(buffer, bufWidth, bufHeight, xPos, digitY, d1, color, digitScale);
    xPos += digitW + spacing;

    // Colon - draw two squares (only if showColon is true for blinking effect)
    if (showColon) {
        int16_t colonX = xPos + colonW / 2;
        int16_t dotSize = digitScale;
        int16_t dotY1 = digitY + digitH / 3 - dotSize / 2;
        int16_t dotY2 = digitY + 2 * digitH / 3 - dotSize / 2;
        drawFilledRect(buffer, bufWidth, bufHeight, colonX - dotSize/2, dotY1, dotSize, dotSize, color);
        drawFilledRect(buffer, bufWidth, bufHeight, colonX - dotSize/2, dotY2, dotSize, dotSize, color);
    }
    xPos += colonW + spacing;

    // Second tens
    drawLargeDigit(buffer, bufWidth, bufHeight, xPos, digitY, d2, color, digitScale);
    xPos += digitW + spacing;

    // Second ones
    drawLargeDigit(buffer, bufWidth, bufHeight, xPos, digitY, d3, color, digitScale);
}

void SettingsMenu::renderWiFiSetup(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight, uint16_t color) {
    // Clear buffer to black
    for (int i = 0; i < bufWidth * bufHeight; i++) {
        buffer[i] = BG_COLOR;
    }

    // Display WiFi setup information
    // Title at top
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, 40, "WIFI SETUP", color);

    // Connection instructions
    int16_t y = 120;
    const int lineSpacing = 45;

    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "CONNECT TO", TEXT_COLOR);
    y += lineSpacing;

    // SSID (larger, in accent color)
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "DESKBUDDY-SETUP", color);
    y += lineSpacing + 15;

    // Password label
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "PASSWORD", TEXT_COLOR);
    y += lineSpacing;

    // Password value
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "DESKBUDDY", color);
    y += lineSpacing + 15;

    // IP address info
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "THEN OPEN", TEXT_COLOR);
    y += lineSpacing;

    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "192.168.4.1", color);
}

void SettingsMenu::renderFirstBootSetup(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight, uint16_t color) {
    // Clear buffer to black
    for (int i = 0; i < bufWidth * bufHeight; i++) {
        buffer[i] = BG_COLOR;
    }

    // Title at top
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, 30, "WIFI SETUP", color);

    // Connection instructions - show SSID, password, and IP clearly
    int16_t y = 90;
    const int lineSpacing = 45;

    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "CONNECT TO", TEXT_COLOR);
    y += lineSpacing;

    // SSID (in accent color)
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "DESKBUDDY-SETUP", color);
    y += lineSpacing + 10;

    // Password label
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "PASSWORD", TEXT_COLOR);
    y += lineSpacing;

    // Password value
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "DESKBUDDY", color);
    y += lineSpacing + 10;

    // IP address info
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "THEN OPEN", TEXT_COLOR);
    y += lineSpacing;

    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "192.168.4.1", color);

    // Hint at bottom
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H - 30, "WAITING FOR CONNECTION", ARROW_COLOR);
}

void SettingsMenu::renderWiFiChoiceScreen(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight, uint16_t color) {
    // Clear buffer to black
    for (int i = 0; i < bufWidth * bufHeight; i++) {
        buffer[i] = BG_COLOR;
    }

    // Title at top
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, 40, "CONNECTED!", color);

    // Subtitle
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, 90, "CHOOSE AN OPTION", TEXT_COLOR);

    // Divider line position
    int16_t dividerY = SCREEN_H / 2;

    // Draw horizontal divider line
    drawFilledRect(buffer, bufWidth, bufHeight, 40, dividerY - 1, SCREEN_W - 80, 2, TEXT_COLOR);

    // Top button area: "Configure WiFi" (above divider)
    int16_t topButtonY = dividerY - 70;
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, topButtonY, "TAP HERE TO", TEXT_COLOR);
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, topButtonY + 40, "CONFIGURE WIFI", color);

    // Bottom button area: "Use Offline" (below divider)
    int16_t bottomButtonY = dividerY + 35;
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, bottomButtonY, "TAP HERE TO", TEXT_COLOR);
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, bottomButtonY + 40, "USE OFFLINE", color);

    // Hint at bottom
    drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H - 30, "AP STAYS ON FOR CONFIG", ARROW_COLOR);
}

//=============================================================================
// Mindfulness Sub-Menu
//=============================================================================

void SettingsMenu::openMindfulSubMenu() {
    mindfulSubMenuOpen = true;
    mindfulSubPage = MINDFUL_PAGE_BREATHE_NOW;
    Serial.println("Mindfulness sub-menu opened");
}

void SettingsMenu::closeMindfulSubMenu() {
    mindfulSubMenuOpen = false;
    mindfulSubPage = 0;
    Serial.println("Mindfulness sub-menu closed");
}

void SettingsMenu::mindfulNextPage() {
    if (mindfulSubPage < MINDFUL_NUM_PAGES - 1) {
        mindfulSubPage++;
        Serial.printf("Mindful page: %d (%s)\n", mindfulSubPage, mindfulPageLabels[mindfulSubPage]);
    }
}

void SettingsMenu::mindfulPrevPage() {
    if (mindfulSubPage > 0) {
        mindfulSubPage--;
        Serial.printf("Mindful page: %d (%s)\n", mindfulSubPage, mindfulPageLabels[mindfulSubPage]);
    }
}

bool SettingsMenu::handleMindfulSubMenuTouch(bool touched, int16_t x, int16_t y) {
    // With 90° CCW rotation: visual vertical = raw X movement
    if (touched && !wasTouched) {
        touchStartX = x;
        touchStartY = y;
        touchCurrentY = y;
        isDraggingSlider = false;
        isSwiping = false;
    }

    if (touched) {
        touchCurrentY = y;
        int16_t deltaX = x - touchStartX;

        // Detect swipe
        if (abs(deltaX) > SWIPE_THRESHOLD && !isDraggingSlider) {
            isSwiping = true;
        }
    }

    if (!touched && wasTouched) {
        int16_t deltaX = x - touchStartX;

        if (isSwiping) {
            // Swipe up (raw X decreases) = next page, swipe down (raw X increases) = prev page
            if (deltaX > SWIPE_THRESHOLD) {
                mindfulPrevPage();
            } else if (deltaX < -SWIPE_THRESHOLD) {
                mindfulNextPage();
            }
        } else if (!isDraggingSlider && !isSwiping) {
            // Tap handling for mindfulness pages
            if (mindfulSubPage == MINDFUL_PAGE_BREATHE_NOW) {
                // Trigger breathing exercise now
                if (breathingExercise != nullptr) {
                    breathingExercise->triggerNow();
                    Serial.println("Breathing triggered from menu");
                    close();  // Close menu to show breathing
                }
            } else if (mindfulSubPage == MINDFUL_PAGE_ENABLE) {
                // Toggle schedule
                if (breathingExercise != nullptr) {
                    breathingExercise->setEnabled(!breathingExercise->isEnabled());
                    Serial.printf("Breathing schedule: %s\n", breathingExercise->isEnabled() ? "ON" : "OFF");
                }
            } else if (mindfulSubPage == MINDFUL_PAGE_SOUND) {
                // Toggle sound
                if (breathingExercise != nullptr) {
                    breathingExercise->setSoundEnabled(!breathingExercise->isSoundEnabled());
                    Serial.printf("Breathing sound: %s\n", breathingExercise->isSoundEnabled() ? "ON" : "OFF");
                }
            } else if (mindfulSubPage == MINDFUL_PAGE_INTERVAL) {
                // Cycle through intervals: 30, 60, 90, 120, 180 minutes
                if (breathingExercise != nullptr) {
                    int currentInterval = breathingExercise->getIntervalMinutes();
                    int newInterval;
                    if (currentInterval < 45) newInterval = 60;
                    else if (currentInterval < 75) newInterval = 90;
                    else if (currentInterval < 105) newInterval = 120;
                    else if (currentInterval < 150) newInterval = 180;
                    else newInterval = 30;
                    breathingExercise->setIntervalMinutes(newInterval);
                    Serial.printf("Breathing interval: %d min\n", newInterval);
                }
            } else if (mindfulSubPage == MINDFUL_PAGE_BACK) {
                closeMindfulSubMenu();
            }
        }

        isDraggingSlider = false;
        isSwiping = false;
    }

    wasTouched = touched;
    return true;
}

void SettingsMenu::renderMindfulSubMenu(uint16_t* buffer, int16_t bufW, int16_t bufH) {
    // Title
    drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, 25, mindfulPageLabels[mindfulSubPage], TEXT_COLOR);

    if (mindfulSubPage == MINDFUL_PAGE_BREATHE_NOW) {
        // Breathe now page
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 - 20, "START A", TEXT_COLOR);
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 20, "BREATHING", TEXT_COLOR);
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 60, "EXERCISE", TEXT_COLOR);
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 50, "TAP TO START", SLIDER_FILL_COLOR);
    } else if (mindfulSubPage == MINDFUL_PAGE_ENABLE) {
        // Schedule enable/disable
        if (breathingExercise != nullptr) {
            const char* status = breathingExercise->isEnabled() ? "ON" : "OFF";
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 - 20, "SCHEDULED", TEXT_COLOR);
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 20, "REMINDERS", TEXT_COLOR);
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 50, status,
                             breathingExercise->isEnabled() ? SLIDER_FILL_COLOR : ARROW_COLOR);
        } else {
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2, "NOT INIT", TEXT_COLOR);
        }
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 80, "TAP TO TOGGLE", ARROW_COLOR);
    } else if (mindfulSubPage == MINDFUL_PAGE_SOUND) {
        // Sound enable/disable
        if (breathingExercise != nullptr) {
            const char* status = breathingExercise->isSoundEnabled() ? "ON" : "OFF";
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 - 20, "BREATHING", TEXT_COLOR);
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 20, "SOUNDS", TEXT_COLOR);
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 50, status,
                             breathingExercise->isSoundEnabled() ? SLIDER_FILL_COLOR : ARROW_COLOR);
        } else {
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2, "NOT INIT", TEXT_COLOR);
        }
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 80, "TAP TO TOGGLE", ARROW_COLOR);
    } else if (mindfulSubPage == MINDFUL_PAGE_INTERVAL) {
        // Interval setting
        if (breathingExercise != nullptr) {
            char intervalStr[32];
            sprintf(intervalStr, "%d MIN", breathingExercise->getIntervalMinutes());
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 - 20, "REMINDER", TEXT_COLOR);
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 20, "INTERVAL", TEXT_COLOR);
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 50, intervalStr, SLIDER_FILL_COLOR);
        } else {
            drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2, "NOT INIT", TEXT_COLOR);
        }
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H - 80, "TAP TO CHANGE", ARROW_COLOR);
    } else if (mindfulSubPage == MINDFUL_PAGE_BACK) {
        // Back page
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 - 15, "TAP TO", TEXT_COLOR);
        drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, SCREEN_H / 2 + 15, "GO BACK", TEXT_COLOR);
    }

    // Page pips - vertical on right side
    int16_t pipX = SCREEN_W - 15;
    int16_t pipSpacing = 25;
    int16_t pipsStartY = SCREEN_H / 2 - (MINDFUL_NUM_PAGES - 1) * pipSpacing / 2;
    for (int i = 0; i < MINDFUL_NUM_PAGES; i++) {
        int16_t pipY = pipsStartY + i * pipSpacing;
        if (i == mindfulSubPage) {
            drawFilledRect(buffer, bufW, bufH, pipX - 5, pipY - 5, 10, 10, TEXT_COLOR);
        } else {
            drawFilledRect(buffer, bufW, bufH, pipX - 3, pipY - 3, 6, 6, ARROW_COLOR);
        }
    }
}

