/**
 * @file wifi_manager.h
 * @brief WiFi state machine for AP/STA mode switching
 *
 * Handles WiFi provisioning flow:
 * - First boot: Start AP mode for setup
 * - Normal boot: Connect to saved credentials
 * - Connection failure: Fall back to AP mode
 *
 * Factory reset: Hold both hardware buttons for 5 seconds
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ESPmDNS.h>

// WiFi connection states
enum class WiFiState {
    Disabled,           // WiFi completely off
    Unconfigured,       // No saved credentials
    APMode,             // Running as access point
    Connecting,         // Attempting to connect to saved network
    Connected,          // Successfully connected to WiFi
    ConnectionFailed    // Failed to connect, will fall back to AP
};

// AP mode configuration
#define WIFI_AP_SSID "DeskBuddy-Setup"
#define WIFI_AP_PASS "deskbuddy"
#define WIFI_AP_IP IPAddress(192, 168, 4, 1)

// Connection settings
#define WIFI_CONNECT_TIMEOUT_MS 15000
#define WIFI_HOSTNAME "deskbuddy"

// Factory reset: button held for this duration
#define FACTORY_RESET_HOLD_MS 5000

/**
 * @class WiFiManager
 * @brief Manages WiFi connection state and provisioning
 */
class WiFiManager {
public:
    WiFiManager();

    /**
     * @brief Initialize WiFi manager
     * @param buttonPin Hardware button pin for factory reset (BOOT button)
     *        Hold for 5 seconds to trigger factory reset
     */
    void begin(int buttonPin = -1);

    /**
     * @brief Check if WiFi credentials are saved
     * @return True if SSID/password stored in Preferences
     */
    bool hasCredentials() const;

    /**
     * @brief Attempt to connect to saved WiFi network
     * Non-blocking - call update() to check progress
     */
    void connectToSavedWiFi();

    /**
     * @brief Start access point mode for provisioning
     */
    void startAPMode();

    /**
     * @brief Update state machine - call from loop()
     */
    void update();

    /**
     * @brief Get current WiFi state
     */
    WiFiState getState() const { return state; }

    /**
     * @brief Get state as human-readable string
     */
    const char* getStateString() const;

    /**
     * @brief Check if connected to WiFi
     */
    bool isConnected() const { return state == WiFiState::Connected; }

    /**
     * @brief Check if in AP mode
     */
    bool isAPMode() const { return state == WiFiState::APMode; }

    /**
     * @brief Get current IP address (STA or AP)
     */
    IPAddress getIP() const;

    /**
     * @brief Get connected SSID (empty if not connected)
     */
    String getSSID() const;

    /**
     * @brief Get signal strength in dBm (0 if not connected)
     */
    int getRSSI() const;

    /**
     * @brief Save new WiFi credentials and attempt connection
     * @param ssid Network SSID
     * @param password Network password
     */
    void saveCredentials(const String& ssid, const String& password);

    /**
     * @brief Clear saved credentials (factory reset)
     */
    void clearCredentials();

    /**
     * @brief Completely disable WiFi (no AP, no STA)
     */
    void disable();

    /**
     * @brief Re-enable WiFi after being disabled
     * Will start AP mode or connect to saved network
     */
    void enable();

    /**
     * @brief Check if WiFi is disabled
     */
    bool isDisabled() const { return state == WiFiState::Disabled; }

    /**
     * @brief Check if factory reset is in progress
     */
    bool isFactoryResetPending() const { return factoryResetPending; }

    /**
     * @brief Get factory reset progress (0.0 to 1.0)
     */
    float getFactoryResetProgress() const;

private:
    WiFiState state;
    Preferences prefs;

    // Saved credentials
    String savedSSID;
    String savedPassword;

    // Connection timing
    unsigned long connectStartTime;

    // Factory reset detection (single button hold)
    int resetButtonPin;
    unsigned long buttonHeldSince;
    bool factoryResetPending;

    // mDNS registered
    bool mdnsStarted;

    void loadCredentials();
    void startMDNS();
    void checkFactoryReset();
};

#endif // WIFI_MANAGER_H
