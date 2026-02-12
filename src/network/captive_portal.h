/**
 * @file captive_portal.h
 * @brief DNS server for captive portal redirect
 *
 * When in AP mode, redirects all DNS queries to the device's IP,
 * causing browsers to automatically open the setup page.
 */

#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include <Arduino.h>
#include <DNSServer.h>

#define DNS_PORT 53

/**
 * @class CaptivePortal
 * @brief DNS-based captive portal for WiFi setup
 */
class CaptivePortal {
public:
    CaptivePortal();

    /**
     * @brief Start the captive portal DNS server
     * @param apIP The IP address to redirect all requests to
     */
    void begin(const IPAddress& apIP);

    /**
     * @brief Stop the captive portal
     */
    void stop();

    /**
     * @brief Process DNS requests - call in loop()
     */
    void update();

    /**
     * @brief Check if portal is running
     */
    bool isRunning() const { return running; }

private:
    DNSServer dnsServer;
    bool running;
};

#endif // CAPTIVE_PORTAL_H
