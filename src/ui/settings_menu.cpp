/**
 * @file settings_menu.cpp
 * @brief Flat settings menu with swipeable pages
 *
 * Buffer is 360x435 (COMBINED_BUF_WIDTH x COMBINED_BUF_HEIGHT).
 * Visible screen is 435 wide x 360 tall (landscape) after 90° CCW rotation.
 *
 * Rotation mapping (90° CCW):
 *   screen (sx, sy) → buffer (sy, bufH - 1 - sx)
 */

#include "settings_menu.h"
#include "text_renderer.h"
#include <cmath>
#include <time.h>
#include <WiFi.h>

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


const char* SettingsMenu::pageLabels[NUM_MENU_PAGES] = {
    "VOLUME",
    "BRIGHT",
    "MIC GAIN",
    "MIC THR",
    "COLOR",
    "TIME",
    "12-24H",
    "TIMEZONE",
    "WIFI",
    "EXIT"
};

SettingsMenu::SettingsMenu()
    : menuOpen(false)
    , currentPage(0)
    , colorIndex(0)
    , timeHour(12)
    , timeMinute(0)
    , is24Hour(false)
    , gmtOffsetHours(0)
    , wifiEnabled(true)
    , offlineModeConfigured(false)
    , settingsVersion(0)
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
    if (currentPage < NUM_MENU_PAGES - 1) {
        currentPage++;
        Serial.printf("Page: %d (%s)\n", currentPage, pageLabels[currentPage]);
    }
}

void SettingsMenu::prevPage() {
    if (currentPage > 0) {
        currentPage--;
        Serial.printf("Page: %d (%s)\n", currentPage, pageLabels[currentPage]);
    }
}

bool SettingsMenu::handleTouch(bool touched, int16_t x, int16_t y) {
    if (!menuOpen) {
        wasTouched = touched;
        return false;
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

        // Handle slider drag for value pages
        if (isDraggingSlider) {
            int newValue;
            switch (currentPage) {
                case PAGE_VOLUME:
                    newValue = ((300 - y) * 100) / 250;
                    values[0] = constrain(newValue, 0, 100);
                    break;
                case PAGE_BRIGHTNESS:
                    newValue = ((300 - y) * 100) / 250;
                    values[1] = constrain(newValue, 0, 100);
                    break;
                case PAGE_MIC_GAIN:
                    newValue = ((300 - y) * 100) / 250;
                    values[2] = constrain(newValue, 0, 100);
                    break;
                case PAGE_MIC_THRESHOLD:
                    newValue = ((300 - y) * 100) / 250;
                    values[3] = constrain(newValue, 0, 100);
                    break;
            }
        }
    } else if (!touched && wasTouched) {
        // Touch ended - use last valid position
        int16_t deltaX = lastX - touchStartX;
        int16_t deltaY = lastY - touchStartY;
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
        } else if (isDraggingSlider && currentPage == PAGE_COLOR) {
            // Horizontal drag on color page: cycle preset
            if (deltaY < -30) {
                colorIndex = (colorIndex + 1) % NUM_COLOR_PRESETS;
                Serial.printf("Color: %s (%d)\n", COLOR_PRESET_NAMES[colorIndex], colorIndex);
            } else if (deltaY > 30) {
                colorIndex = (colorIndex + NUM_COLOR_PRESETS - 1) % NUM_COLOR_PRESETS;
                Serial.printf("Color: %s (%d)\n", COLOR_PRESET_NAMES[colorIndex], colorIndex);
            }
        } else if (isDraggingSlider && currentPage == PAGE_TIME) {
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
        } else if (isDraggingSlider && currentPage == PAGE_TIMEZONE) {
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
            if (currentPage == PAGE_TIME) {
                // Tap left half = increment hour, tap right half = increment minute
                if (lastY > 175) {
                    timeHour = (timeHour + 1) % 24;
                    Serial.printf("Time: %02d:%02d (hour++)\n", timeHour, timeMinute);
                } else {
                    timeMinute = (timeMinute + 1) % 60;
                    Serial.printf("Time: %02d:%02d (min++)\n", timeHour, timeMinute);
                }
            } else if (currentPage == PAGE_TIME_FORMAT) {
                is24Hour = !is24Hour;
                Serial.printf("Time format: %s\n", is24Hour ? "24H" : "12H");
            } else if (currentPage == PAGE_TIMEZONE) {
                // Tap left half = decrement, tap right half = increment
                if (lastY > SCREEN_W / 2) {
                    gmtOffsetHours = constrain(gmtOffsetHours + 1, -12, 14);
                } else {
                    gmtOffsetHours = constrain(gmtOffsetHours - 1, -12, 14);
                }
                Serial.printf("Timezone: UTC%+d\n", gmtOffsetHours);
                saveSettings();
            } else if (currentPage == PAGE_WIFI) {
                wifiEnabled = !wifiEnabled;
                Serial.printf("WiFi: %s\n", wifiEnabled ? "ON" : "OFF");
                saveSettings();
            } else if (currentPage == PAGE_EXIT) {
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

    TextRenderer::clearBuffer(buffer, bufWidth, bufHeight, BG_COLOR);

    // Title
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, 25, pageLabels[currentPage], TEXT_COLOR);

    if (currentPage >= PAGE_VOLUME && currentPage <= PAGE_MIC_THRESHOLD) {
        // Horizontal slider pages
        int sliderIdx = currentPage;  // 0-3 maps to values[0-3]

        int16_t sliderX = 50;
        int16_t sliderW = SCREEN_W - 100;
        int16_t sliderY = SCREEN_H / 2 - 15;
        int16_t sliderH = 30;

        TextRenderer::drawFilledRect(buffer, bufWidth, bufHeight, sliderX, sliderY, sliderW, sliderH, SLIDER_BG_COLOR);

        int16_t fillW = (sliderW * values[sliderIdx]) / 100;
        TextRenderer::drawFilledRect(buffer, bufWidth, bufHeight, sliderX, sliderY, fillW, sliderH, SLIDER_FILL_COLOR);

        // Center marker for mic gain (0dB position)
        if (currentPage == PAGE_MIC_GAIN) {
            int16_t centerX = sliderX + sliderW / 2;
            int16_t markerW = 3;
            int16_t markerH = sliderH + 20;
            int16_t markerY = sliderY - 10;
            TextRenderer::drawFilledRect(buffer, bufWidth, bufHeight, centerX - markerW / 2, markerY, markerW, markerH, TEXT_COLOR);
        }

        // Knob
        int16_t knobW = 24;
        int16_t knobH = 50;
        int16_t knobX = sliderX + fillW - knobW / 2;
        knobX = constrain(knobX, sliderX - knobW/2, sliderX + sliderW - knobW/2);
        int16_t knobY = sliderY - 10;
        TextRenderer::drawFilledRect(buffer, bufWidth, bufHeight, knobX, knobY, knobW, knobH, KNOB_COLOR);

        // Value display
        char valStr[16];
        if (currentPage == PAGE_MIC_GAIN) {
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
        TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H - 50, valStr, TEXT_COLOR);

        // Show mic level on threshold page
        if (currentPage == PAGE_MIC_THRESHOLD) {
            char micStr[16];
            sprintf(micStr, "LEVEL %d", (int)(micLevel * 100));
            TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, 60, micStr, TEXT_COLOR);

            int16_t levelBarX = 50;
            int16_t levelBarW = SCREEN_W - 100;
            int16_t levelBarY = 80;
            int16_t levelBarH = 10;

            TextRenderer::drawFilledRect(buffer, bufWidth, bufHeight, levelBarX, levelBarY, levelBarW, levelBarH, SLIDER_BG_COLOR);

            int16_t levelFillW = (int16_t)(levelBarW * micLevel);
            if (levelFillW > 0) {
                TextRenderer::drawFilledRect(buffer, bufWidth, bufHeight, levelBarX, levelBarY, levelFillW, levelBarH,
                               micLevel > (values[3] / 100.0f) ? 0xF800 : SLIDER_FILL_COLOR);
            }
        }
    } else if (currentPage == PAGE_COLOR) {
        // Eye mockup with selected color
        uint16_t eyeCol = COLOR_PRESETS[colorIndex];
        int16_t eyeW = 60;
        int16_t eyeH = 80;
        int16_t eyeSpacing = 50;
        int16_t eyeCenterY = SCREEN_H / 3;
        int16_t leftEyeX = SCREEN_W / 2 - eyeSpacing / 2 - eyeW;
        int16_t rightEyeX = SCREEN_W / 2 + eyeSpacing / 2;
        int16_t eyeY = eyeCenterY - eyeH / 2;

        TextRenderer::drawFilledRect(buffer, bufWidth, bufHeight, leftEyeX, eyeY, eyeW, eyeH, eyeCol);
        TextRenderer::drawFilledRect(buffer, bufWidth, bufHeight, rightEyeX, eyeY, eyeW, eyeH, eyeCol);

        TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 + 30, COLOR_PRESET_NAMES[colorIndex], TEXT_COLOR);
        TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 + 80, "SWIPE LR", ARROW_COLOR);
    } else if (currentPage == PAGE_TIME) {
        drawTimeDisplay(buffer, bufWidth, bufHeight);
        TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H - 40, "TAP TO SET", ARROW_COLOR);
    } else if (currentPage == PAGE_TIME_FORMAT) {
        const char* formatStr = is24Hour ? "24 HOUR" : "12 HOUR";
        TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 - 30, formatStr, SLIDER_FILL_COLOR);

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
        TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 + 20, exampleStr, TEXT_COLOR);
        TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H - 40, "TAP TO TOGGLE", ARROW_COLOR);
    } else if (currentPage == PAGE_TIMEZONE) {
        // Timezone offset display
        char tzStr[16];
        if (gmtOffsetHours >= 0) {
            sprintf(tzStr, "UTC+%d", gmtOffsetHours);
        } else {
            sprintf(tzStr, "UTC%d", gmtOffsetHours);
        }
        TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 - 30, tzStr, SLIDER_FILL_COLOR);
        TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 + 20, "FOR NTP SYNC", TEXT_COLOR);
        TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H - 40, "TAP +/- OR DRAG", ARROW_COLOR);
    } else if (currentPage == PAGE_WIFI) {
        // WiFi on/off toggle with connection info
        const char* wifiStatus = wifiEnabled ? "WIFI ON" : "WIFI OFF";
        uint16_t statusColor = wifiEnabled ? SLIDER_FILL_COLOR : ARROW_COLOR;
        TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 - 60, wifiStatus, statusColor);

        if (wifiEnabled) {
            if (WiFi.isConnected()) {
                // Show connected SSID and IP
                String ssid = WiFi.SSID();
                if (ssid.length() > 20) ssid = ssid.substring(0, 20);
                TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 - 20, ssid.c_str(), TEXT_COLOR);
                String ip = WiFi.localIP().toString();
                TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 + 15, ip.c_str(), SLIDER_FILL_COLOR);
            } else {
                TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 - 20, "AP MODE", TEXT_COLOR);
                String apIp = WiFi.softAPIP().toString();
                TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 + 15, apIp.c_str(), SLIDER_FILL_COLOR);
            }
        } else {
            TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 - 10, "NO CONNECTION", TEXT_COLOR);
        }
        TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H - 40, "TAP TO TOGGLE", ARROW_COLOR);
    } else if (currentPage == PAGE_EXIT) {
        TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 - 15, "TAP TO", TEXT_COLOR);
        TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H / 2 + 15, "CLOSE", TEXT_COLOR);
    }

    // Page pips - vertical on right side
    int16_t pipX = SCREEN_W - 15;
    int16_t pipSpacing = 22;
    int16_t pipsStartY = SCREEN_H / 2 - (NUM_MENU_PAGES - 1) * pipSpacing / 2;
    for (int i = 0; i < NUM_MENU_PAGES; i++) {
        int16_t pipY = pipsStartY + i * pipSpacing;
        if (i == currentPage) {
            // Current page: larger bright pip
            TextRenderer::drawFilledRect(buffer, bufWidth, bufHeight, pipX - 5, pipY - 5, 10, 10, TEXT_COLOR);
        } else {
            // Other pages: small dim pip
            TextRenderer::drawFilledRect(buffer, bufWidth, bufHeight, pipX - 3, pipY - 3, 6, 6, ARROW_COLOR);
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

bool SettingsMenu::getTime(int& hour, int& minute) const {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        hour = timeinfo.tm_hour;
        minute = timeinfo.tm_min;
        return true;   // NTP-synced
    }
    hour = timeHour;
    minute = timeMinute;
    return false;  // Fallback (unreliable)
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
    TextRenderer::drawLargeDigit(buffer, bufW, bufH, xPos, digitY, d0, SLIDER_FILL_COLOR, digitScale);
    xPos += digitW + spacing;

    // Hour ones
    TextRenderer::drawLargeDigit(buffer, bufW, bufH, xPos, digitY, d1, SLIDER_FILL_COLOR, digitScale);
    xPos += digitW + spacing;

    // Colon
    TextRenderer::drawCenteredText(buffer, bufW, bufH, xPos + colonW / 2, digitY + digitH / 3, ":", TEXT_COLOR);
    xPos += colonW + spacing;

    // Minute tens
    TextRenderer::drawLargeDigit(buffer, bufW, bufH, xPos, digitY, d2, SLIDER_FILL_COLOR, digitScale);
    xPos += digitW + spacing;

    // Minute ones
    TextRenderer::drawLargeDigit(buffer, bufW, bufH, xPos, digitY, d3, SLIDER_FILL_COLOR, digitScale);

    // Show AM/PM for 12-hour mode
    if (!is24Hour) {
        const char* ampm = (hour >= 12) ? "PM" : "AM";
        TextRenderer::drawCenteredText(buffer, bufW, bufH, SCREEN_W / 2, digitY + digitH + 20, ampm, ARROW_COLOR);
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

void SettingsMenu::renderTimeOnly(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight, uint16_t color, bool showColon) {
    TextRenderer::clearBuffer(buffer, bufWidth, bufHeight, BG_COLOR);

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
    TextRenderer::drawLargeDigit(buffer, bufWidth, bufHeight, xPos, digitY, d0, color, digitScale);
    xPos += digitW + spacing;

    // Hour ones
    TextRenderer::drawLargeDigit(buffer, bufWidth, bufHeight, xPos, digitY, d1, color, digitScale);
    xPos += digitW + spacing;

    // Colon - draw two squares (only if showColon is true for blinking effect)
    if (showColon) {
        int16_t colonX = xPos + colonW / 2;
        int16_t dotSize = digitScale;
        int16_t dotY1 = digitY + digitH / 3 - dotSize / 2;
        int16_t dotY2 = digitY + 2 * digitH / 3 - dotSize / 2;
        TextRenderer::drawFilledRect(buffer, bufWidth, bufHeight, colonX - dotSize/2, dotY1, dotSize, dotSize, color);
        TextRenderer::drawFilledRect(buffer, bufWidth, bufHeight, colonX - dotSize/2, dotY2, dotSize, dotSize, color);
    }
    xPos += colonW + spacing;

    // Minute tens
    TextRenderer::drawLargeDigit(buffer, bufWidth, bufHeight, xPos, digitY, d2, color, digitScale);
    xPos += digitW + spacing;

    // Minute ones
    TextRenderer::drawLargeDigit(buffer, bufWidth, bufHeight, xPos, digitY, d3, color, digitScale);
}

void SettingsMenu::renderCountdown(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight,
                                    int minutes, int seconds, uint16_t color, bool showColon,
                                    const char* label) {
    TextRenderer::clearBuffer(buffer, bufWidth, bufHeight, BG_COLOR);

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
        TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, labelY, label, color);
    }

    // Get digits (MM:SS format)
    int d0 = (minutes / 10) % 10;
    int d1 = minutes % 10;
    int d2 = (seconds / 10) % 10;
    int d3 = seconds % 10;

    // Draw all digits in the eye color
    int16_t xPos = startX;

    // Minute tens
    TextRenderer::drawLargeDigit(buffer, bufWidth, bufHeight, xPos, digitY, d0, color, digitScale);
    xPos += digitW + spacing;

    // Minute ones
    TextRenderer::drawLargeDigit(buffer, bufWidth, bufHeight, xPos, digitY, d1, color, digitScale);
    xPos += digitW + spacing;

    // Colon - draw two squares (only if showColon is true for blinking effect)
    if (showColon) {
        int16_t colonX = xPos + colonW / 2;
        int16_t dotSize = digitScale;
        int16_t dotY1 = digitY + digitH / 3 - dotSize / 2;
        int16_t dotY2 = digitY + 2 * digitH / 3 - dotSize / 2;
        TextRenderer::drawFilledRect(buffer, bufWidth, bufHeight, colonX - dotSize/2, dotY1, dotSize, dotSize, color);
        TextRenderer::drawFilledRect(buffer, bufWidth, bufHeight, colonX - dotSize/2, dotY2, dotSize, dotSize, color);
    }
    xPos += colonW + spacing;

    // Second tens
    TextRenderer::drawLargeDigit(buffer, bufWidth, bufHeight, xPos, digitY, d2, color, digitScale);
    xPos += digitW + spacing;

    // Second ones
    TextRenderer::drawLargeDigit(buffer, bufWidth, bufHeight, xPos, digitY, d3, color, digitScale);
}

void SettingsMenu::renderWiFiSetup(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight, uint16_t color) {
    TextRenderer::clearBuffer(buffer, bufWidth, bufHeight, BG_COLOR);

    // Display WiFi setup information
    // Title at top
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, 20, "WIFI SETUP", color);

    // Connection instructions
    int16_t y = 65;
    const int lineSpacing = 36;
    const int sectionGap = 42;

    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "CONNECT TO", TEXT_COLOR);
    y += lineSpacing;

    // SSID (larger, in accent color)
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "DESKBUDDY-SETUP", color);
    y += sectionGap;

    // Password label
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "PASSWORD", TEXT_COLOR);
    y += lineSpacing;

    // Password value
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "DESKBUDDY", color);
    y += sectionGap;

    // IP address info
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "THEN OPEN", TEXT_COLOR);
    y += lineSpacing;

    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "192.168.4.1", color);
}

void SettingsMenu::renderFirstBootSetup(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight, uint16_t color) {
    TextRenderer::clearBuffer(buffer, bufWidth, bufHeight, BG_COLOR);

    // Title at top
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, 20, "WIFI SETUP", color);

    // Connection instructions - show SSID, password, and IP clearly
    int16_t y = 65;
    const int lineSpacing = 36;
    const int sectionGap = 42;

    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "CONNECT TO", TEXT_COLOR);
    y += lineSpacing;

    // SSID (in accent color)
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "DESKBUDDY-SETUP", color);
    y += sectionGap;

    // Password label
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "PASSWORD", TEXT_COLOR);
    y += lineSpacing;

    // Password value
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "DESKBUDDY", color);
    y += sectionGap;

    // IP address info
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "THEN OPEN", TEXT_COLOR);
    y += lineSpacing;

    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, y, "192.168.4.1", color);

    // Hint at bottom
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H - 20, "WAITING FOR CONNECTION", ARROW_COLOR);
}

void SettingsMenu::renderWiFiChoiceScreen(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight, uint16_t color) {
    TextRenderer::clearBuffer(buffer, bufWidth, bufHeight, BG_COLOR);

    // Title at top
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, 40, "CONNECTED!", color);

    // Subtitle
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, 90, "CHOOSE AN OPTION", TEXT_COLOR);

    // Divider line position
    int16_t dividerY = SCREEN_H / 2;

    // Draw horizontal divider line
    TextRenderer::drawFilledRect(buffer, bufWidth, bufHeight, 40, dividerY - 1, SCREEN_W - 80, 2, TEXT_COLOR);

    // Top button area: "Configure WiFi" (above divider)
    int16_t topButtonY = dividerY - 70;
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, topButtonY, "TAP HERE TO", TEXT_COLOR);
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, topButtonY + 40, "CONFIGURE WIFI", color);

    // Bottom button area: "Use Offline" (below divider)
    int16_t bottomButtonY = dividerY + 35;
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, bottomButtonY, "TAP HERE TO", TEXT_COLOR);
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, bottomButtonY + 40, "USE OFFLINE", color);

    // Hint at bottom
    TextRenderer::drawCenteredText(buffer, bufWidth, bufHeight, SCREEN_W / 2, SCREEN_H - 30, "AP STAYS ON FOR CONFIG", ARROW_COLOR);
}
