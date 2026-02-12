/**
 * @file network_setup.h
 * @brief WiFi lifecycle orchestration
 *
 * Manages WiFi enable/disable, NTP sync, captive portal lifecycle,
 * first-boot setup flow, and timezone change handling.
 */

#ifndef NETWORK_SETUP_H
#define NETWORK_SETUP_H

#include <Arduino.h>

// Forward declarations
class WiFiManager;
class CaptivePortal;
class SettingsMenu;

class NetworkSetup {
public:
    NetworkSetup();
    void begin(WiFiManager& wifi, CaptivePortal& portal, SettingsMenu& settings);

    /**
     * @brief Update WiFi state machine, NTP sync, captive portal, timezone, toggle
     * @return true if a full screen clear is needed
     */
    bool update();

    // --- First-boot screen state ---
    bool isShowingSetup() const { return showingWiFiSetup; }
    bool isShowingChoice() const { return showingWiFiChoice; }

    /**
     * @brief Check if an AP client connected (transitions setup → choice)
     * @return true if screen changed (needs full screen clear)
     */
    bool checkAPClientConnected();

    /**
     * @brief Handle touch on WiFi choice screen
     * @param touching Whether screen is currently touched
     * @param effectiveY Touch Y in buffer coords
     * @param bufH Buffer height (for splitting top/bottom halves)
     * @return true if a choice was made (needs full screen clear)
     */
    bool handleChoiceTouch(bool touching, int16_t effectiveY, int16_t bufH);

private:
    WiFiManager* pWifi;
    CaptivePortal* pPortal;
    SettingsMenu* pSettings;

    bool showingWiFiSetup;
    bool showingWiFiChoice;
    bool touchWasActive;
    int lastAPClientCount;
    bool wifiWasEnabled;
    bool wifiWasConnected;
    int8_t lastGmtOffsetHours;
};

#endif // NETWORK_SETUP_H
