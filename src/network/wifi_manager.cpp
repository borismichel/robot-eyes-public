/**
 * @file wifi_manager.cpp
 * @brief WiFi state machine with multi-network support
 *
 * Manages WiFi connectivity with automatic provisioning flow:
 *
 * First Boot / No Credentials:
 *   Unconfigured → APMode (starts "DeskBuddy-Setup" network)
 *
 * Normal Boot with Saved Credentials:
 *   Scan → Try saved networks in priority order → Connected (starts mDNS)
 *                                               → All failed → APMode (fallback)
 *
 * Connection Lost:
 *   Connected → Connecting (auto-reconnect current network)
 *
 * Factory Reset (hold BOOT button 5s):
 *   Any state → clears all credentials → ESP.restart() → APMode
 *
 * Credentials stored in NVS "wifi" namespace as indexed keys:
 *   count, ssid0/pass0, ssid1/pass1, ..., ssid4/pass4
 */

#include "wifi_manager.h"
#include <time.h>
#include <algorithm>

// Global pointer for static event callback
static WiFiManager* g_wifiManagerInstance = nullptr;

WiFiManager::WiFiManager()
    : state(WiFiState::Unconfigured)
    , currentNetworkIndex(-1)
    , connectStartTime(0)
    , connectingFromAP(false)
    , apShutdownTime(0)
    , resetButtonPin(-1)
    , buttonHeldSince(0)
    , factoryResetPending(false)
    , mdnsStarted(false)
    , ntpStarted(false)
    , lastDisconnectReason(0)
{
}

void WiFiManager::begin(int buttonPin) {
    resetButtonPin = buttonPin;

    if (resetButtonPin >= 0) {
        pinMode(resetButtonPin, INPUT_PULLUP);
    }

    WiFi.setHostname(WIFI_HOSTNAME);

    g_wifiManagerInstance = this;
    WiFi.onEvent(onWiFiEvent, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    loadCredentials();

    if (hasCredentials()) {
        Serial.printf("[WiFi] Found %d saved network(s)\n", savedNetworks.size());
        for (const auto& net : savedNetworks) {
            Serial.printf("[WiFi]   - %s\n", net.ssid);
        }
    } else {
        Serial.println("[WiFi] No saved credentials found");
        state = WiFiState::Unconfigured;
    }
}

//=============================================================================
// Credential Storage (NVS)
//=============================================================================

void WiFiManager::loadCredentials() {
    prefs.begin("wifi", true);
    int count = prefs.getInt("count", -1);

    if (count < 0) {
        // Migration: check for old single-credential format
        String oldSSID = prefs.getString("ssid", "");
        String oldPass = prefs.getString("pass", "");
        prefs.end();

        if (oldSSID.length() > 0) {
            Serial.println("[WiFi] Migrating single credential to multi-network format");
            WiFiNetwork net;
            strncpy(net.ssid, oldSSID.c_str(), 32);
            net.ssid[32] = '\0';
            strncpy(net.password, oldPass.c_str(), 64);
            net.password[64] = '\0';
            savedNetworks.push_back(net);
            saveAllCredentials();

            // Clean up old keys
            prefs.begin("wifi", false);
            prefs.remove("ssid");
            prefs.remove("pass");
            prefs.end();
        }
        return;
    }

    savedNetworks.clear();
    for (int i = 0; i < count && i < MAX_SAVED_NETWORKS; i++) {
        char keySSID[8], keyPass[8];
        sprintf(keySSID, "ssid%d", i);
        sprintf(keyPass, "pass%d", i);
        String s = prefs.getString(keySSID, "");
        String p = prefs.getString(keyPass, "");
        if (s.length() > 0) {
            WiFiNetwork net;
            strncpy(net.ssid, s.c_str(), 32);
            net.ssid[32] = '\0';
            strncpy(net.password, p.c_str(), 64);
            net.password[64] = '\0';
            savedNetworks.push_back(net);
        }
    }
    prefs.end();
}

void WiFiManager::saveAllCredentials() {
    prefs.begin("wifi", false);
    prefs.clear();
    prefs.putInt("count", savedNetworks.size());
    for (int i = 0; i < (int)savedNetworks.size(); i++) {
        char keySSID[8], keyPass[8];
        sprintf(keySSID, "ssid%d", i);
        sprintf(keyPass, "pass%d", i);
        prefs.putString(keySSID, savedNetworks[i].ssid);
        prefs.putString(keyPass, savedNetworks[i].password);
    }
    prefs.end();
    Serial.printf("[WiFi] Saved %d network(s) to NVS\n", savedNetworks.size());
}

bool WiFiManager::hasCredentials() const {
    return !savedNetworks.empty();
}

void WiFiManager::saveCredentials(const String& ssid, const String& password) {
    // Update existing if SSID already saved
    for (auto& net : savedNetworks) {
        if (String(net.ssid) == ssid) {
            strncpy(net.password, password.c_str(), 64);
            net.password[64] = '\0';
            Serial.printf("[WiFi] Updated password for: %s\n", ssid.c_str());
            saveAllCredentials();
            connectToSavedWiFi();
            return;
        }
    }

    // Add new network at front (highest priority)
    if ((int)savedNetworks.size() >= MAX_SAVED_NETWORKS) {
        Serial.printf("[WiFi] Removing oldest network: %s\n",
                      savedNetworks.back().ssid);
        savedNetworks.pop_back();
    }

    WiFiNetwork net;
    strncpy(net.ssid, ssid.c_str(), 32);
    net.ssid[32] = '\0';
    strncpy(net.password, password.c_str(), 64);
    net.password[64] = '\0';
    savedNetworks.insert(savedNetworks.begin(), net);

    Serial.printf("[WiFi] Added network: %s (%d total)\n",
                  ssid.c_str(), savedNetworks.size());
    saveAllCredentials();
    connectToSavedWiFi();
}

void WiFiManager::removeNetwork(const String& ssid) {
    auto it = std::remove_if(savedNetworks.begin(), savedNetworks.end(),
        [&](const WiFiNetwork& n) { return String(n.ssid) == ssid; });
    if (it != savedNetworks.end()) {
        savedNetworks.erase(it, savedNetworks.end());
        saveAllCredentials();
        Serial.printf("[WiFi] Removed network: %s\n", ssid.c_str());
    }
}

void WiFiManager::clearCredentials() {
    prefs.begin("wifi", false);
    prefs.clear();
    prefs.end();

    savedNetworks.clear();
    Serial.println("[WiFi] All credentials cleared");
}

//=============================================================================
// Connection Management
//=============================================================================

void WiFiManager::connectToSavedWiFi() {
    if (savedNetworks.empty()) {
        Serial.println("[WiFi] No credentials to connect with");
        state = WiFiState::Unconfigured;
        return;
    }

    // Quick scan to find which saved networks are available
    Serial.println("[WiFi] Scanning for available networks...");
    int n = WiFi.scanNetworks(false, false, false, 300);
    scannedSSIDs.clear();
    for (int i = 0; i < n; i++) {
        scannedSSIDs.push_back(WiFi.SSID(i));
    }
    WiFi.scanDelete();
    Serial.printf("[WiFi] Found %d networks in range\n", n);

    // Find first saved network that appears in scan results
    currentNetworkIndex = -1;
    for (int i = 0; i < (int)savedNetworks.size(); i++) {
        for (const auto& scanned : scannedSSIDs) {
            if (scanned == String(savedNetworks[i].ssid)) {
                currentNetworkIndex = i;
                break;
            }
        }
        if (currentNetworkIndex >= 0) break;
    }

    if (currentNetworkIndex < 0) {
        if (savedNetworks.size() == 1) {
            // Only one network saved — try it anyway (might be hidden)
            currentNetworkIndex = 0;
        } else {
            // No saved network found in scan
            Serial.println("[WiFi] No saved networks found in scan - entering AP mode");
            startAPMode();
            return;
        }
    }

    tryCurrentNetwork();
}

void WiFiManager::tryCurrentNetwork() {
    if (currentNetworkIndex < 0 || currentNetworkIndex >= (int)savedNetworks.size()) {
        Serial.println("[WiFi] All saved networks tried - entering AP mode");
        startAPMode();
        return;
    }

    const auto& net = savedNetworks[currentNetworkIndex];
    Serial.printf("[WiFi] Trying network %d/%d: %s\n",
                  currentNetworkIndex + 1, (int)savedNetworks.size(), net.ssid);

    connectingFromAP = (state == WiFiState::APMode);
    lastDisconnectReason = 0;

    WiFi.disconnect(false);
    delay(100);

    if (connectingFromAP) {
        WiFi.mode(WIFI_AP_STA);
        Serial.println("[WiFi] Using AP+STA mode (AP stays alive for feedback)");
    } else {
        WiFi.mode(WIFI_STA);
    }

    WiFi.begin(net.ssid, net.password);
    state = WiFiState::Connecting;
    connectStartTime = millis();
}

void WiFiManager::disable() {
    Serial.println("[WiFi] Disabling WiFi completely");
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

void WiFiManager::startAPMode() {
    Serial.println("[WiFi] Starting AP mode");
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(WIFI_AP_IP, WIFI_AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);

    Serial.printf("[WiFi] AP started - SSID: %s, IP: %s\n",
                  WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());

    state = WiFiState::APMode;
    connectingFromAP = false;
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

//=============================================================================
// State Machine Update
//=============================================================================

void WiFiManager::update() {
    checkFactoryReset();

    switch (state) {
        case WiFiState::Connecting: {
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
                state = WiFiState::Connected;
                startMDNS();

                if (connectingFromAP) {
                    apShutdownTime = millis() + 10000;
                    Serial.println("[WiFi] AP will shut down in 10 seconds");
                }
            }
            else if (millis() - connectStartTime > WIFI_CONNECT_TIMEOUT_MS) {
                Serial.printf("[WiFi] Timeout connecting to: %s\n",
                              savedNetworks[currentNetworkIndex].ssid);

                // Try next saved network that was seen in scan
                currentNetworkIndex++;
                while (currentNetworkIndex < (int)savedNetworks.size()) {
                    bool found = false;
                    for (const auto& scanned : scannedSSIDs) {
                        if (scanned == String(savedNetworks[currentNetworkIndex].ssid)) {
                            found = true;
                            break;
                        }
                    }
                    if (found) break;
                    currentNetworkIndex++;
                }

                if (currentNetworkIndex < (int)savedNetworks.size()) {
                    tryCurrentNetwork();
                } else {
                    Serial.println("[WiFi] All networks failed - entering AP mode");
                    state = WiFiState::ConnectionFailed;
                    if (connectingFromAP) {
                        state = WiFiState::APMode;
                        WiFi.mode(WIFI_AP);
                    } else {
                        startAPMode();
                    }
                    connectingFromAP = false;
                }
            }
            break;
        }

        case WiFiState::Connected: {
            // Delayed AP shutdown after connecting from AP mode
            if (apShutdownTime > 0 && millis() >= apShutdownTime) {
                Serial.println("[WiFi] Shutting down AP (STA connected)");
                WiFi.softAPdisconnect(true);
                WiFi.mode(WIFI_STA);
                apShutdownTime = 0;
                connectingFromAP = false;
            }

            // Monitor connection and reconnect if lost
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WiFi] Connection lost - reconnecting...");
                mdnsStarted = false;
                apShutdownTime = 0;
                connectToSavedWiFi();
            }
            break;
        }

        case WiFiState::Disabled:
        case WiFiState::APMode:
        case WiFiState::Unconfigured:
        case WiFiState::ConnectionFailed:
            break;
    }
}

//=============================================================================
// Factory Reset
//=============================================================================

void WiFiManager::checkFactoryReset() {
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
            delay(500);
            ESP.restart();
        }
    } else {
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

//=============================================================================
// Status Queries
//=============================================================================

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

//=============================================================================
// NTP
//=============================================================================

void WiFiManager::syncNTP(long gmtOffsetSec) {
    if (state != WiFiState::Connected) {
        Serial.println("[WiFi] Cannot sync NTP - not connected");
        return;
    }

    Serial.printf("[WiFi] Starting NTP sync (GMT offset: %ld seconds)\n", gmtOffsetSec);
    configTime(gmtOffsetSec, 0, "pool.ntp.org", "time.google.com");
    ntpStarted = true;

    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 5000)) {
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
    if (!getLocalTime(&timeinfo, 0)) return false;
    return timeinfo.tm_year >= 124;
}

//=============================================================================
// Event Handler
//=============================================================================

void WiFiManager::onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (!g_wifiManagerInstance) return;
    uint8_t reason = info.wifi_sta_disconnected.reason;
    g_wifiManagerInstance->lastDisconnectReason = reason;
    Serial.printf("[WiFi] Disconnected, reason: %d\n", reason);
}

const char* WiFiManager::getFailReason() const {
    switch (lastDisconnectReason) {
        case 0:   return "";
        case 2:   return "Authentication expired";
        case 15:  return "Wrong password (handshake timeout)";
        case 201: return "Network not found";
        case 202: return "Wrong password";
        case 203: return "Association rejected by access point";
        case 204: return "Wrong password (handshake failed)";
        case 205: return "Connection failed";
        default:  return "Connection failed";
    }
}
