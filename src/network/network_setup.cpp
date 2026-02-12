/**
 * @file network_setup.cpp
 * @brief WiFi lifecycle orchestration implementation
 */

#include "network_setup.h"
#include "wifi_manager.h"
#include "captive_portal.h"
#include "../ui/settings_menu.h"

NetworkSetup::NetworkSetup()
    : pWifi(nullptr)
    , pPortal(nullptr)
    , pSettings(nullptr)
    , showingWiFiSetup(false)
    , showingWiFiChoice(false)
    , touchWasActive(false)
    , lastAPClientCount(0)
    , wifiWasEnabled(true)
    , wifiWasConnected(false)
    , lastGmtOffsetHours(0)
{
}

void NetworkSetup::begin(WiFiManager& wifi, CaptivePortal& portal, SettingsMenu& settings) {
    pWifi = &wifi;
    pPortal = &portal;
    pSettings = &settings;

    wifiWasEnabled = pSettings->isWiFiEnabled();
    lastGmtOffsetHours = pSettings->getGmtOffsetHours();

    if (!pSettings->isWiFiEnabled()) {
        Serial.println("WiFi disabled in settings - staying offline");
        pWifi->disable();
    } else if (pWifi->hasCredentials()) {
        Serial.println("Connecting to saved WiFi...");
        pWifi->connectToSavedWiFi();
    } else {
        Serial.println("No WiFi credentials - starting AP mode");
        pWifi->startAPMode();
        pPortal->begin(WIFI_AP_IP);
        Serial.println("Captive portal started");

        // Show first-boot setup screen only if user hasn't chosen offline mode
        if (!pSettings->isOfflineModeConfigured()) {
            showingWiFiSetup = true;
            Serial.println("First boot - showing WiFi setup screen");
        }
    }
}

bool NetworkSetup::update() {
    bool needScreenClear = false;

    // Update WiFi state machine
    pWifi->update();

    // Trigger NTP sync when WiFi first connects
    bool wifiNowConnected = pWifi->isConnected();
    if (wifiNowConnected && !wifiWasConnected) {
        pWifi->syncNTP(pSettings->getGmtOffsetHours() * 3600L);
    }
    wifiWasConnected = wifiNowConnected;

    // Update captive portal (only when in AP mode)
    if (pWifi->isAPMode()) {
        if (!pPortal->isRunning()) {
            pPortal->begin(WIFI_AP_IP);
            Serial.println("Captive portal started");
        }
        pPortal->update();
    } else if (pPortal->isRunning()) {
        pPortal->stop();
        Serial.println("Captive portal stopped");
    }

    // Handle timezone change - re-sync NTP
    int8_t currentGmtOffset = pSettings->getGmtOffsetHours();
    if (currentGmtOffset != lastGmtOffsetHours) {
        lastGmtOffsetHours = currentGmtOffset;
        if (pWifi->isConnected()) {
            Serial.printf("Timezone changed to UTC%+d - re-syncing NTP\n", currentGmtOffset);
            pWifi->syncNTP(currentGmtOffset * 3600L);
        }
    }

    // Handle WiFi enable/disable toggle from settings
    bool wifiNowEnabled = pSettings->isWiFiEnabled();
    if (wifiNowEnabled != wifiWasEnabled) {
        if (wifiNowEnabled) {
            Serial.println("WiFi enabled from settings");
            pWifi->enable();
            if (pWifi->isAPMode() && !pPortal->isRunning()) {
                pPortal->begin(WIFI_AP_IP);
            }
        } else {
            Serial.println("WiFi disabled from settings");
            if (pPortal->isRunning()) {
                pPortal->stop();
            }
            pWifi->disable();
        }
        wifiWasEnabled = wifiNowEnabled;
        needScreenClear = true;
    }

    return needScreenClear;
}

bool NetworkSetup::checkAPClientConnected() {
    if (!showingWiFiSetup) return false;

    int currentAPClients = WiFi.softAPgetStationNum();
    if (currentAPClients > lastAPClientCount) {
        Serial.printf("AP client connected! (%d clients) - showing choice screen\n", currentAPClients);
        showingWiFiSetup = false;
        showingWiFiChoice = true;
        lastAPClientCount = currentAPClients;
        return true;
    }
    lastAPClientCount = currentAPClients;
    return false;
}

bool NetworkSetup::handleChoiceTouch(bool touching, int16_t effectiveY, int16_t bufH) {
    if (!showingWiFiChoice) return false;

    bool choiceMade = false;
    if (touching && !touchWasActive) {
        if (effectiveY < bufH / 2) {
            // Top half - Configure WiFi
            Serial.println("Configure WiFi selected - keeping AP mode for setup");
            showingWiFiChoice = false;
            choiceMade = true;
        } else {
            // Bottom half - Use Offline
            Serial.println("Use Offline selected - eyes will show, AP stays running");
            pSettings->setOfflineModeConfigured(true);
            showingWiFiChoice = false;
            choiceMade = true;
        }
    }
    touchWasActive = touching;
    return choiceMade;
}
