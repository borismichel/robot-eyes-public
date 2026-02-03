/**
 * @file captive_portal.cpp
 * @brief Captive portal DNS server implementation
 *
 * Provides automatic redirect to setup page when device is in AP mode.
 * When active, all DNS queries (any domain) resolve to the AP's IP address.
 * This causes most devices to detect a "captive portal" and automatically
 * open the setup page when connecting to the DeskBuddy-Setup network.
 *
 * Usage:
 *   1. Start when entering AP mode: captivePortal.begin(WiFi.softAPIP())
 *   2. Call captivePortal.update() in main loop
 *   3. Stop when exiting AP mode: captivePortal.stop()
 */

#include "captive_portal.h"

CaptivePortal::CaptivePortal()
    : running(false)
{
}

void CaptivePortal::begin(const IPAddress& apIP) {
    if (running) {
        return;
    }

    // Start DNS server - redirect all domains to our IP
    dnsServer.start(DNS_PORT, "*", apIP);
    running = true;

    Serial.printf("[CaptivePortal] Started - redirecting all DNS to %s\n",
                  apIP.toString().c_str());
}

void CaptivePortal::stop() {
    if (!running) {
        return;
    }

    dnsServer.stop();
    running = false;

    Serial.println("[CaptivePortal] Stopped");
}

void CaptivePortal::update() {
    if (running) {
        dnsServer.processNextRequest();
    }
}
