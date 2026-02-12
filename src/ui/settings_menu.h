/**
 * @file settings_menu.h
 * @brief Flat settings menu with swipeable pages
 *
 * Menu Structure (10 pages, flat):
 *   Volume, Brightness, Mic Gain, Mic Threshold, Color,
 *   Time, Time Format, Timezone, WiFi, Exit
 *
 * Navigation: Swipe up/down between pages, tap to select or toggle.
 * Horizontal sliders for value adjustment on settings pages.
 * All settings persisted via Preferences library.
 */

#ifndef SETTINGS_MENU_H
#define SETTINGS_MENU_H

#include <Arduino.h>
#include <Preferences.h>
#include "../eyes/eye_renderer.h"
#include "text_renderer.h"

// Menu pages (flat)
#define NUM_MENU_PAGES 10
#define PAGE_VOLUME      0
#define PAGE_BRIGHTNESS  1
#define PAGE_MIC_GAIN    2
#define PAGE_MIC_THRESHOLD 3
#define PAGE_COLOR       4
#define PAGE_TIME        5
#define PAGE_TIME_FORMAT 6
#define PAGE_TIMEZONE    7
#define PAGE_WIFI        8
#define PAGE_EXIT        9

// Swipe detection
#define SWIPE_THRESHOLD 40  // Minimum pixels to register a swipe

/**
 * @class SettingsMenu
 * @brief Flat settings menu with swipeable pages
 *
 * Also provides utility rendering functions:
 * - renderTimeOnly(): Display current time (HH:MM format)
 * - renderCountdown(): Display pomodoro countdown (MM:SS format with optional label)
 */
class SettingsMenu {
public:
    SettingsMenu();

    void begin();
    bool isOpen() const { return menuOpen; }
    void open();
    void close();
    void toggle();

    /**
     * @brief Handle touch input
     * @return True if touch was consumed by menu
     */
    bool handleTouch(bool touched, int16_t x, int16_t y);

    /**
     * @brief Render current page to framebuffer
     * @param micLevel Current microphone level (0.0-1.0) for display on threshold page
     */
    void render(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight,
                int16_t bufScreenX, int16_t bufScreenY, float micLevel = 0.0f);

    // Getters
    int getVolume() const { return values[0]; }
    int getBrightness() const { return values[1]; }
    int getMicSensitivity() const { return values[2]; }
    int getMicThreshold() const { return values[3]; }
    int getColorIndex() const { return colorIndex; }
    uint16_t getColorRGB565() const;
    int getTimeHour() const;
    int getTimeMinute() const;
    /** Atomic time read. Returns true if NTP-synced, false if fallback. */
    bool getTime(int& hour, int& minute) const;
    bool is24HourFormat() const { return is24Hour; }

    // WiFi settings
    bool isWiFiEnabled() const { return wifiEnabled; }
    bool isOfflineModeConfigured() const { return offlineModeConfigured; }
    void setWiFiEnabled(bool enabled);
    void setOfflineModeConfigured(bool configured);

    /**
     * @brief Get settings version (increments on any change)
     * Used for detecting changes from web interface
     */
    uint32_t getSettingsVersion() const { return settingsVersion; }

    /**
     * @brief Render only the time display (for periodic display)
     * @param color RGB565 color for the digits (use eye color)
     * @param showColon Whether to draw the colon (for blinking effect)
     */
    void renderTimeOnly(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight, uint16_t color, bool showColon = true);

    /**
     * @brief Render a countdown timer (MM:SS format)
     */
    void renderCountdown(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight,
                         int minutes, int seconds, uint16_t color, bool showColon = true,
                         const char* label = nullptr);

    /**
     * @brief Render WiFi setup screen for AP mode
     */
    void renderWiFiSetup(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight, uint16_t color);

    /**
     * @brief Render first-boot WiFi info screen
     */
    void renderFirstBootSetup(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight, uint16_t color);

    /**
     * @brief Render WiFi choice screen with interactive buttons
     */
    void renderWiFiChoiceScreen(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight, uint16_t color);

    /**
     * @brief Advance time by specified minutes (for clock tick)
     */
    void tickMinute() { addMinutes(1); }

    // Setters
    void setVolume(int val);
    void setBrightness(int val);
    void setMicSensitivity(int val);
    void setMicThreshold(int val);

    // Time setters (for web interface)
    void setTime(int hour, int minute);
    void setTimeFormat(bool use24Hour);

    // Timezone getter/setter
    int8_t getGmtOffsetHours() const { return gmtOffsetHours; }
    void setGmtOffsetHours(int8_t hours);

    // Color setter (for web interface)
    void setColorIndex(int index);

private:
    bool menuOpen;
    int currentPage;        // 0-9
    int values[4];          // Volume, Brightness, Mic Gain, Mic Threshold (0-100)
    int colorIndex;         // Index into COLOR_PRESETS (0-7)
    int timeHour;           // 0-23
    int timeMinute;         // 0-59
    bool is24Hour;          // True for 24-hour format
    int8_t gmtOffsetHours;  // Timezone offset in hours (-12 to +14)
    bool wifiEnabled;       // True to enable WiFi (AP or STA mode)
    bool offlineModeConfigured;  // True if user chose "Use Offline" on first boot
    uint32_t settingsVersion;  // Increments on any change (for web sync)
    Preferences prefs;

    // Touch state
    bool wasTouched;
    int16_t touchStartX;
    int16_t touchStartY;
    int16_t touchCurrentY;
    bool isDraggingSlider;
    bool isSwiping;

    // Page labels
    static const char* pageLabels[NUM_MENU_PAGES];

    void saveSettings();
    void loadSettings();
    void nextPage();
    void prevPage();

    // Update slider from vertical touch position
    void updateSliderFromTouch(int16_t y, int16_t bufHeight);

    // Time page helpers
    void drawTimeDisplay(uint16_t* buffer, int16_t bufW, int16_t bufH);
    void addMinutes(int minutes);
};

#endif // SETTINGS_MENU_H
