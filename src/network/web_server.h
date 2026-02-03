/**
 * @file web_server.h
 * @brief HTTP web server for settings management
 *
 * Uses ESP-IDF's native HTTP server (esp_http_server) for compatibility
 * with Arduino ESP32 3.x framework.
 *
 * Endpoints:
 * - GET /               - Main settings page
 * - GET /api/settings   - Get all settings as JSON
 * - POST /api/settings  - Update settings
 * - GET /api/status     - Device status (WiFi, pomodoro)
 * - GET /api/time       - Get current time
 * - POST /api/time      - Set time (hour, minute, is24Hour)
 * - GET /api/wifi/scan  - Scan for WiFi networks
 * - POST /api/wifi/connect - Connect to new WiFi
 * - POST /api/wifi/forget  - Clear saved WiFi credentials
 * - POST /api/pomodoro/start - Start pomodoro timer
 * - POST /api/pomodoro/stop  - Stop pomodoro timer
 */

#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <esp_http_server.h>
#include <ArduinoJson.h>

// Forward declarations
class SettingsMenu;
class PomodoroTimer;
class WiFiManager;

// Expression preview callback type
typedef void (*ExpressionCallback)(int expressionIndex);

/**
 * @class WebServerManager
 * @brief HTTP server for remote settings management
 */
class WebServerManager {
public:
    WebServerManager();
    ~WebServerManager();

    /**
     * @brief Start the web server
     * @param settings Pointer to SettingsMenu for reading/writing settings
     * @param pomodoro Pointer to PomodoroTimer for status/control
     * @param wifi Pointer to WiFiManager for WiFi operations
     * @return true if server started successfully
     */
    bool begin(SettingsMenu* settings, PomodoroTimer* pomodoro, WiFiManager* wifi);

    /**
     * @brief Stop the web server
     */
    void stop();

    /**
     * @brief Check if server is running
     */
    bool isRunning() const { return server != nullptr; }

    /**
     * @brief Check if settings were changed via web interface
     * Call this in loop() and apply changes if true
     */
    bool hasSettingsChange();

    /**
     * @brief Clear the settings changed flag after applying changes
     */
    void clearSettingsChange() { settingsChanged = false; }

    /**
     * @brief Set callback for expression preview
     * @param callback Function to call when expression preview is requested
     */
    void setExpressionCallback(ExpressionCallback callback) { expressionCallback = callback; }

private:
    ExpressionCallback expressionCallback;
    httpd_handle_t server;
    SettingsMenu* settingsMenu;
    PomodoroTimer* pomodoroTimer;
    WiFiManager* wifiManager;
    bool settingsChanged;

    // Static handler wrappers (esp_http_server requires C-style callbacks)
    static esp_err_t handleRoot(httpd_req_t* req);
    static esp_err_t handleGetSettings(httpd_req_t* req);
    static esp_err_t handlePostSettings(httpd_req_t* req);
    static esp_err_t handleGetStatus(httpd_req_t* req);
    static esp_err_t handleWiFiScan(httpd_req_t* req);
    static esp_err_t handleWiFiConnect(httpd_req_t* req);
    static esp_err_t handleWiFiForget(httpd_req_t* req);
    static esp_err_t handleGetTime(httpd_req_t* req);
    static esp_err_t handlePostTime(httpd_req_t* req);
    static esp_err_t handlePomodoroStart(httpd_req_t* req);
    static esp_err_t handlePomodoroStop(httpd_req_t* req);
    static esp_err_t handlePostExpression(httpd_req_t* req);

    // Helper to get WebServerManager instance from request context
    static WebServerManager* getInstance(httpd_req_t* req);

    // Generate settings page HTML
    String generateSettingsPage();

    // Build JSON responses
    void buildSettingsJson(JsonDocument& doc);
    void buildStatusJson(JsonDocument& doc);
};

#endif // WEB_SERVER_H
