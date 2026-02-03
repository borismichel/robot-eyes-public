/**
 * @file settings_menu.h
 * @brief Hierarchical settings menu with sub-menus
 *
 * Menu Structure:
 * - Main Menu (3 pages): Pomodoro, Settings, Exit
 * - Pomodoro Sub-Menu (7 pages): Start/Stop, Work, Short Break, Long Break, Sessions, Ticking, Back
 * - Settings Sub-Menu (8 pages): Volume, Brightness, Mic Gain, Mic Threshold, Color, Time, Time Format, Back
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

// Forward declaration
class PomodoroTimer;

// Main menu pages
#define NUM_MAIN_PAGES 3
#define PAGE_POMODORO 0
#define PAGE_SETTINGS 1
#define PAGE_EXIT 2

// Pomodoro sub-menu pages
#define POMO_NUM_PAGES 7
#define POMO_PAGE_START_STOP 0
#define POMO_PAGE_WORK 1
#define POMO_PAGE_SHORT_BREAK 2
#define POMO_PAGE_LONG_BREAK 3
#define POMO_PAGE_SESSIONS 4
#define POMO_PAGE_TICKING 5
#define POMO_PAGE_BACK 6

// Settings sub-menu pages
#define SETTINGS_NUM_PAGES 9
#define SETTINGS_PAGE_VOLUME 0
#define SETTINGS_PAGE_BRIGHTNESS 1
#define SETTINGS_PAGE_MIC_GAIN 2
#define SETTINGS_PAGE_MIC_THRESHOLD 3
#define SETTINGS_PAGE_COLOR 4
#define SETTINGS_PAGE_TIME 5
#define SETTINGS_PAGE_TIME_FORMAT 6
#define SETTINGS_PAGE_WIFI 7
#define SETTINGS_PAGE_BACK 8

// Swipe detection
#define SWIPE_THRESHOLD 40  // Minimum pixels to register a swipe

/**
 * @class SettingsMenu
 * @brief Hierarchical settings menu with Pomodoro and Settings sub-menus
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

    // Set pomodoro timer reference (must be called after begin)
    void setPomodoroTimer(PomodoroTimer* timer) { pomodoroTimer = timer; }

    // Getters (indices offset by 1 due to Pomodoro page at 0)
    int getVolume() const { return values[0]; }      // Volume is at values[0]
    int getBrightness() const { return values[1]; }  // Brightness at values[1]
    int getMicSensitivity() const { return values[2]; }  // Mic Gain at values[2]
    int getMicThreshold() const { return values[3]; }    // Mic Threshold at values[3]
    int getColorIndex() const { return colorIndex; }
    uint16_t getColorRGB565() const;
    int getTimeHour() const { return timeHour; }
    int getTimeMinute() const { return timeMinute; }
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
     * @param minutes Minutes to display (0-99)
     * @param seconds Seconds to display (0-59)
     * @param color RGB565 color for the digits
     * @param showColon Whether to draw the colon (for blinking effect)
     * @param label Optional label to show above timer (e.g., "WORK", "BREAK")
     */
    void renderCountdown(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight,
                         int minutes, int seconds, uint16_t color, bool showColon = true,
                         const char* label = nullptr);

    /**
     * @brief Render WiFi setup screen for AP mode
     * Displays SSID, password, and IP address for initial configuration
     * @param color RGB565 color for the text
     */
    void renderWiFiSetup(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight, uint16_t color);

    /**
     * @brief Render first-boot setup screen with interactive buttons
     * Shows AP info plus "Configure WiFi" and "Use Offline" button areas
     * @param color RGB565 color for accent text
     */
    void renderFirstBootSetup(uint16_t* buffer, int16_t bufWidth, int16_t bufHeight, uint16_t color);

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

    // Color setter (for web interface)
    void setColorIndex(int index);

private:
    bool menuOpen;
    int currentPage;        // 0-8
    int values[4];          // Volume, Brightness, Mic Gain, Mic Threshold (0-100)
    PomodoroTimer* pomodoroTimer;  // Reference to pomodoro timer (set externally)
    int colorIndex;         // Index into COLOR_PRESETS (0-7)
    int timeHour;           // 0-23
    int timeMinute;         // 0-59
    bool is24Hour;          // True for 24-hour format
    bool wifiEnabled;       // True to enable WiFi (AP or STA mode)
    bool offlineModeConfigured;  // True if user chose "Use Offline" on first boot
    uint32_t settingsVersion;  // Increments on any change (for web sync)
    Preferences prefs;

    // Pomodoro sub-menu state
    bool pomoSubMenuOpen;   // True when in pomodoro sub-menu
    int pomoSubPage;        // 0-6 within sub-menu
    static const char* pomoPageLabels[POMO_NUM_PAGES];

    // Settings sub-menu state
    bool settingsSubMenuOpen;  // True when in settings sub-menu
    int settingsSubPage;       // 0-7 within sub-menu
    static const char* settingsPageLabels[SETTINGS_NUM_PAGES];

    // Touch state
    bool wasTouched;
    int16_t touchStartX;
    int16_t touchStartY;
    int16_t touchCurrentY;
    bool isDraggingSlider;
    bool isSwiping;

    // Page labels
    static const char* mainPageLabels[NUM_MAIN_PAGES];

    void saveSettings();
    void loadSettings();
    void nextPage();
    void prevPage();

    // Pomodoro sub-menu helpers
    void openPomoSubMenu();
    void closePomoSubMenu();
    void pomoNextPage();
    void pomoPrevPage();
    void renderPomoSubMenu(uint16_t* buffer, int16_t bufW, int16_t bufH);
    bool handlePomoSubMenuTouch(bool touched, int16_t x, int16_t y);

    // Settings sub-menu helpers
    void openSettingsSubMenu();
    void closeSettingsSubMenu();
    void settingsNextPage();
    void settingsPrevPage();
    void renderSettingsSubMenu(uint16_t* buffer, int16_t bufW, int16_t bufH, float micLevel);
    bool handleSettingsSubMenuTouch(bool touched, int16_t x, int16_t y);

    // Update slider from vertical touch position
    void updateSliderFromTouch(int16_t y, int16_t bufHeight);

    // Drawing helpers (with XY swap and 3x font)
    void drawFilledRect(uint16_t* buffer, int16_t bufW, int16_t bufH,
                        int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
    void drawText(uint16_t* buffer, int16_t bufW, int16_t bufH,
                  int16_t x, int16_t y, const char* text, uint16_t color);
    void drawChar(uint16_t* buffer, int16_t bufW, int16_t bufH,
                  int16_t x, int16_t y, char c, uint16_t color);
    void drawCenteredText(uint16_t* buffer, int16_t bufW, int16_t bufH,
                          int16_t centerX, int16_t y, const char* text, uint16_t color);

    // Time page helpers
    void drawLargeDigit(uint16_t* buffer, int16_t bufW, int16_t bufH,
                        int16_t x, int16_t y, int digit, uint16_t color, int scale = 5);
    void drawTimeDisplay(uint16_t* buffer, int16_t bufW, int16_t bufH);
    void addMinutes(int minutes);
};

#endif // SETTINGS_MENU_H
