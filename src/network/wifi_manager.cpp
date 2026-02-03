/**
 * @file wifi_manager.cpp
 * @brief WiFi state machine implementation
 *
 * Manages WiFi connectivity with automatic provisioning flow:
 *
 * First Boot / No Credentials:
 *   Unconfigured → APMode (starts "DeskBuddy-Setup" network)
 *
 * Normal Boot with Saved Credentials:
 *   Unconfigured → Connecting → Connected (starts mDNS: deskbuddy.local)
 *                            → ConnectionFailed → APMode (fallback)
 *
 * Connection Lost:
 *   Connected → Connecting (auto-reconnect)
 *
 * Factory Reset (hold BOOT button 5s):
 *   Any state → clears credentials → ESP.restart() → APMode
 *
 * Credentials are stored in NVS (Preferences library) under "wifi" namespace.
 */

#include "wifi_manager.h"
#include <time.h>

WiFiManager::WiFiManager()
    : state(WiFiState::Unconfigured)
    , connectStartTime(0)
    , resetButtonPin(-1)
    , buttonHeldSince(0)
    , factoryResetPending(false)
    , mdnsStarted(false)
    , ntpStarted(false)
{
}

void WiFiManager::begin(int buttonPin) {
    resetButtonPin = buttonPin;

    // Configure button pin if provided
    if (resetButtonPin >= 0) {
        pinMode(resetButtonPin, INPUT_PULLUP);
    }

    // Set hostname before any WiFi operations
    WiFi.setHostname(WIFI_HOSTNAME);

    // Load saved credentials
    loadCredentials();

    if (hasCredentials()) {
        Serial.printf("[WiFi] Found saved credentials for: %s\n", savedSSID.c_str());
    } else {
        Serial.println("[WiFi] No saved credentials found");
        state = WiFiState::Unconfigured;
    }
}

void WiFiManager::loadCredentials() {
    prefs.begin("wifi", true);  // Read-only
    savedSSID = prefs.getString("ssid", "");
    savedPassword = prefs.getString("pass", "");
    prefs.end();
}

bool WiFiManager::hasCredentials() const {
    return savedSSID.length() > 0;
}

void WiFiManager::saveCredentials(const String& ssid, const String& password) {
    prefs.begin("wifi", false);  // Read-write
    prefs.putString("ssid", ssid);
    prefs.putString("pass", password);
    prefs.end();

    savedSSID = ssid;
    savedPassword = password;

    Serial.printf("[WiFi] Saved credentials for: %s\n", ssid.c_str());

    // Attempt connection with new credentials
    connectToSavedWiFi();
}

void WiFiManager::clearCredentials() {
    prefs.begin("wifi", false);
    prefs.clear();
    prefs.end();

    savedSSID = "";
    savedPassword = "";

    Serial.println("[WiFi] Credentials cleared");
}

void WiFiManager::disable() {
    Serial.println("[WiFi] Disabling WiFi completely");

    // Stop any existing connection
    WiFi.disconnect(true);
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);

    mdnsStarted = false;
    state = WiFiState::Disabled;

    Serial.println("[WiFi] Disabled");
}

void WiFiManager::enable() {
    if (state != WiFiState::Disabled) {
        Serial.println("[WiFi] Already enabled");
        return;
    }

    Serial.println("[WiFi] Re-enabling WiFi");

    if (hasCredentials()) {
        connectToSavedWiFi();
    } else {
        startAPMode();
    }
}

void WiFiManager::connectToSavedWiFi() {
    if (!hasCredentials()) {
        Serial.println("[WiFi] No credentials to connect with");
        state = WiFiState::Unconfigured;
        return;
    }

    Serial.printf("[WiFi] Connecting to: %s\n", savedSSID.c_str());

    // Disconnect from any previous connection
    WiFi.disconnect(true);
    delay(100);

    // Start connection
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

    state = WiFiState::Connecting;
    connectStartTime = millis();
}

void WiFiManager::startAPMode() {
    Serial.println("[WiFi] Starting AP mode");

    // Stop any existing connection
    WiFi.disconnect(true);
    delay(100);

    // Configure and start AP
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);

    Serial.printf("[WiFi] AP started - SSID: %s, IP: %s\n",
                  WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());

    state = WiFiState::APMode;
}

void WiFiManager::startMDNS() {
    if (mdnsStarted) return;

    if (MDNS.begin(WIFI_HOSTNAME)) {
        MDNS.addService("http", "tcp", 80);
        mdnsStarted = true;
        Serial.printf("[WiFi] mDNS started: %s.local\n", WIFI_HOSTNAME);
    } else {
        Serial.println("[WiFi] mDNS failed to start");
    }
}

void WiFiManager::update() {
    // Check for factory reset (both buttons held)
    checkFactoryReset();

    switch (state) {
        case WiFiState::Connecting: {
            // Check if connected
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
                state = WiFiState::Connected;
                startMDNS();
            }
            // Check for timeout
            else if (millis() - connectStartTime > WIFI_CONNECT_TIMEOUT_MS) {
                Serial.println("[WiFi] Connection timeout - falling back to AP mode");
                state = WiFiState::ConnectionFailed;
                startAPMode();
            }
            break;
        }

        case WiFiState::Connected: {
            // Monitor connection and reconnect if lost
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WiFi] Connection lost - reconnecting...");
                mdnsStarted = false;
                connectToSavedWiFi();
            }
            break;
        }

        case WiFiState::Disabled:
        case WiFiState::APMode:
        case WiFiState::Unconfigured:
        case WiFiState::ConnectionFailed:
            // Nothing to do in these states
            break;
    }
}

void WiFiManager::checkFactoryReset() {
    // Need button configured
    if (resetButtonPin < 0) return;

    bool buttonPressed = (digitalRead(resetButtonPin) == LOW);

    if (buttonPressed) {
        if (buttonHeldSince == 0) {
            buttonHeldSince = millis();
            Serial.println("[WiFi] Factory reset: button pressed...");
        }

        unsigned long heldDuration = millis() - buttonHeldSince;

        if (heldDuration >= FACTORY_RESET_HOLD_MS && !factoryResetPending) {
            factoryResetPending = true;
            Serial.println("[WiFi] Factory reset triggered!");

            clearCredentials();

            // Restart to AP mode
            delay(500);
            ESP.restart();
        }
    } else {
        // Button released before threshold
        if (buttonHeldSince > 0 && !factoryResetPending) {
            Serial.println("[WiFi] Factory reset cancelled");
        }
        buttonHeldSince = 0;
    }
}

float WiFiManager::getFactoryResetProgress() const {
    if (buttonHeldSince == 0) return 0.0f;

    unsigned long held = millis() - buttonHeldSince;
    float progress = (float)held / (float)FACTORY_RESET_HOLD_MS;
    return min(progress, 1.0f);
}

const char* WiFiManager::getStateString() const {
    switch (state) {
        case WiFiState::Disabled:         return "Disabled";
        case WiFiState::Unconfigured:     return "Unconfigured";
        case WiFiState::APMode:           return "AP Mode";
        case WiFiState::Connecting:       return "Connecting";
        case WiFiState::Connected:        return "Connected";
        case WiFiState::ConnectionFailed: return "Connection Failed";
        default:                          return "Unknown";
    }
}

IPAddress WiFiManager::getIP() const {
    if (state == WiFiState::APMode) {
        return WiFi.softAPIP();
    }
    return WiFi.localIP();
}

String WiFiManager::getSSID() const {
    if (state == WiFiState::Connected) {
        return WiFi.SSID();
    }
    return "";
}

int WiFiManager::getRSSI() const {
    if (state == WiFiState::Connected) {
        return WiFi.RSSI();
    }
    return 0;
}

void WiFiManager::syncNTP(long gmtOffsetSec) {
    if (state != WiFiState::Connected) {
        Serial.println("[WiFi] Cannot sync NTP - not connected");
        return;
    }

    Serial.printf("[WiFi] Starting NTP sync (GMT offset: %ld seconds)\n", gmtOffsetSec);
    configTime(gmtOffsetSec, 0, "pool.ntp.org", "time.google.com");
    ntpStarted = true;

    // Wait briefly for initial sync (non-blocking check)
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5000)) {  // Wait up to 5 seconds
        Serial.printf("[WiFi] NTP synced: %04d-%02d-%02d %02d:%02d:%02d\n",
                      timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                      timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        Serial.println("[WiFi] NTP sync pending (will continue in background)");
    }
}

bool WiFiManager::isNtpSynced() const {
    if (!ntpStarted) return false;

    struct tm timeinfo;
    // Quick check with 0 timeout
    if (!getLocalTime(&timeinfo, 0)) {
        return false;
    }
    // Valid if year is reasonable (2024+)
    return timeinfo.tm_year >= 124;  // tm_year is years since 1900
}
