/**
 * @file wifi_manager.h
 * @brief WiFi state machine for AP/STA mode switching with multi-network support
 *
 * Handles WiFi provisioning flow:
 * - First boot: Start AP mode for setup
 * - Normal boot: Scan, then try saved networks in priority order
 * - All networks fail: Fall back to AP mode
 *
 * Stores up to 5 WiFi networks in NVS.
 * Factory reset: Hold BOOT button for 5 seconds
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <esp_wifi_types.h>
#include <vector>

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
#define WIFI_CONNECT_TIMEOUT_MS 10000
#define WIFI_HOSTNAME "deskbuddy"
#define MAX_SAVED_NETWORKS 5

// Factory reset: button held for this duration
#define FACTORY_RESET_HOLD_MS 5000

/**
 * @struct WiFiNetwork
 * @brief Saved WiFi network credentials
 */
struct WiFiNetwork {
    char ssid[33];        // Max SSID length + null
    char password[65];    // Max WPA password length + null
};

/**
 * @class WiFiManager
 * @brief Manages WiFi connection state with multi-network support
 */
class WiFiManager {
public:
    WiFiManager();

    void begin(int buttonPin = -1);

    bool hasCredentials() const;
    void connectToSavedWiFi();
    void startAPMode();
    void update();

    // State queries
    WiFiState getState() const { return state; }
    const char* getStateString() const;
    bool isConnected() const { return state == WiFiState::Connected; }
    bool isAPMode() const { return state == WiFiState::APMode; }
    bool isDisabled() const { return state == WiFiState::Disabled; }
    IPAddress getIP() const;
    String getSSID() const;
    int getRSSI() const;

    // Network management
    void saveCredentials(const String& ssid, const String& password);
    void clearCredentials();
    void removeNetwork(const String& ssid);
    int getNetworkCount() const { return savedNetworks.size(); }
    const std::vector<WiFiNetwork>& getSavedNetworks() const { return savedNetworks; }

    // WiFi control
    void disable();
    void enable();

    // Failure info
    const char* getFailReason() const;

    // Factory reset
    bool isFactoryResetPending() const { return factoryResetPending; }
    float getFactoryResetProgress() const;

    // NTP
    void syncNTP(long gmtOffsetSec);
    bool isNtpSynced() const;

private:
    WiFiState state;
    Preferences prefs;

    // Saved networks (priority order: index 0 = highest)
    std::vector<WiFiNetwork> savedNetworks;

    // Connection state
    int currentNetworkIndex;
    std::vector<String> scannedSSIDs;
    unsigned long connectStartTime;
    bool connectingFromAP;
    unsigned long apShutdownTime;

    // Factory reset detection
    int resetButtonPin;
    unsigned long buttonHeldSince;
    bool factoryResetPending;

    // mDNS / NTP
    bool mdnsStarted;
    bool ntpStarted;

    // Last disconnect reason
    uint8_t lastDisconnectReason;

    void loadCredentials();
    void saveAllCredentials();
    void tryCurrentNetwork();
    void startMDNS();
    void checkFactoryReset();
    static void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
};

#endif // WIFI_MANAGER_H
