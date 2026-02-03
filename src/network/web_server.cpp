/**
 * @file web_server.cpp
 * @brief HTTP web server implementation using ESP-IDF native server
 *
 * This module provides a web-based configuration interface for DeskBuddy.
 * Uses ESP-IDF's native esp_http_server for compatibility with Arduino ESP32 3.x.
 *
 * Web UI Features:
 * - Tabbed interface: Dashboard, Display, Audio, Time, WiFi, Pomodoro, Expressions
 * - Real-time status updates via polling /api/status every second
 * - Settings sync with version tracking to detect external changes
 * - Expression preview grid for all 30 expressions
 * - Eye color picker matching device COLOR_PRESETS order
 *
 * REST API:
 * - GET  /               - Serves the single-page web application
 * - GET  /api/settings   - Returns all device settings as JSON
 * - POST /api/settings   - Updates device settings (volume, brightness, etc.)
 * - GET  /api/status     - Returns WiFi, pomodoro, time, and uptime status
 * - GET  /api/time       - Returns current device time
 * - POST /api/time       - Sets device time (hour, minute, is24Hour)
 * - GET  /api/wifi/scan  - Scans for available WiFi networks
 * - POST /api/wifi/connect - Connects to a new WiFi network
 * - POST /api/wifi/forget  - Clears saved WiFi credentials
 * - POST /api/pomodoro/start - Starts the pomodoro timer
 * - POST /api/pomodoro/stop  - Stops the pomodoro timer
 * - POST /api/expression - Previews an expression on device (index: 0-29)
 *
 * Design System:
 * - Dark theme: #0A0A0A background, #F2F2F2 foreground, #DFFF00 accent
 * - Fonts: JetBrains Mono (labels/monospace), Inter (body text)
 * - Swiss-style minimalist aesthetic with status cards
 */

#include "web_server.h"
#include "wifi_manager.h"
#include "../ui/settings_menu.h"
#include "../ui/pomodoro.h"
#include <WiFi.h>

WebServerManager::WebServerManager()
    : server(nullptr)
    , settingsMenu(nullptr)
    , pomodoroTimer(nullptr)
    , wifiManager(nullptr)
    , settingsChanged(false)
    , expressionCallback(nullptr)
    , audioTestCallback(nullptr)
    , moodGetterCallback(nullptr)
{
}

WebServerManager::~WebServerManager() {
    stop();
}

bool WebServerManager::begin(SettingsMenu* settings, PomodoroTimer* pomodoro, WiFiManager* wifi) {
    settingsMenu = settings;
    pomodoroTimer = pomodoro;
    wifiManager = wifi;

    if (server != nullptr) {
        Serial.println("[WebServer] Already running");
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 15;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        Serial.printf("[WebServer] Failed to start: %s\n", esp_err_to_name(err));
        return false;
    }

    // Store this instance in server context for static handlers
    httpd_config_t* serverConfig = (httpd_config_t*)server;

    // Register URI handlers
    httpd_uri_t rootUri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = handleRoot,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &rootUri);

    httpd_uri_t getSettingsUri = {
        .uri = "/api/settings",
        .method = HTTP_GET,
        .handler = handleGetSettings,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &getSettingsUri);

    httpd_uri_t postSettingsUri = {
        .uri = "/api/settings",
        .method = HTTP_POST,
        .handler = handlePostSettings,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &postSettingsUri);

    httpd_uri_t statusUri = {
        .uri = "/api/status",
        .method = HTTP_GET,
        .handler = handleGetStatus,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &statusUri);

    httpd_uri_t wifiScanUri = {
        .uri = "/api/wifi/scan",
        .method = HTTP_GET,
        .handler = handleWiFiScan,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &wifiScanUri);

    httpd_uri_t wifiConnectUri = {
        .uri = "/api/wifi/connect",
        .method = HTTP_POST,
        .handler = handleWiFiConnect,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &wifiConnectUri);

    httpd_uri_t wifiForgetUri = {
        .uri = "/api/wifi/forget",
        .method = HTTP_POST,
        .handler = handleWiFiForget,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &wifiForgetUri);

    httpd_uri_t wifiDisableUri = {
        .uri = "/api/wifi/disable",
        .method = HTTP_POST,
        .handler = handleWiFiDisable,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &wifiDisableUri);

    httpd_uri_t getTimeUri = {
        .uri = "/api/time",
        .method = HTTP_GET,
        .handler = handleGetTime,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &getTimeUri);

    httpd_uri_t postTimeUri = {
        .uri = "/api/time",
        .method = HTTP_POST,
        .handler = handlePostTime,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &postTimeUri);

    httpd_uri_t pomodoroStartUri = {
        .uri = "/api/pomodoro/start",
        .method = HTTP_POST,
        .handler = handlePomodoroStart,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &pomodoroStartUri);

    httpd_uri_t pomodoroStopUri = {
        .uri = "/api/pomodoro/stop",
        .method = HTTP_POST,
        .handler = handlePomodoroStop,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &pomodoroStopUri);

    httpd_uri_t expressionUri = {
        .uri = "/api/expression",
        .method = HTTP_POST,
        .handler = handlePostExpression,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &expressionUri);

    httpd_uri_t audioTestUri = {
        .uri = "/api/audio/test",
        .method = HTTP_POST,
        .handler = handleAudioTest,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &audioTestUri);

    Serial.printf("[WebServer] Started on port %d\n", config.server_port);
    return true;
}

void WebServerManager::stop() {
    if (server != nullptr) {
        httpd_stop(server);
        server = nullptr;
        Serial.println("[WebServer] Stopped");
    }
}

bool WebServerManager::hasSettingsChange() {
    return settingsChanged;
}

WebServerManager* WebServerManager::getInstance(httpd_req_t* req) {
    return (WebServerManager*)req->user_ctx;
}

// ============================================================================
// HTTP Handlers
// ============================================================================

esp_err_t WebServerManager::handleRoot(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);
    String html = self->generateSettingsPage();

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html.c_str(), html.length());
    return ESP_OK;
}

esp_err_t WebServerManager::handleGetSettings(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    JsonDocument doc;
    self->buildSettingsJson(doc);

    String json;
    serializeJson(doc, json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

esp_err_t WebServerManager::handlePostSettings(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    // Read request body
    char content[512];
    int received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
        return ESP_FAIL;
    }
    content[received] = '\0';

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, content);
    if (error) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Apply device settings
    if (doc["volume"].is<int>()) {
        self->settingsMenu->setVolume(doc["volume"].as<int>());
    }
    if (doc["brightness"].is<int>()) {
        self->settingsMenu->setBrightness(doc["brightness"].as<int>());
    }
    if (doc["micGain"].is<int>()) {
        self->settingsMenu->setMicSensitivity(doc["micGain"].as<int>());
    }
    if (doc["micThreshold"].is<int>()) {
        self->settingsMenu->setMicThreshold(doc["micThreshold"].as<int>());
    }
    if (doc["eyeColorIndex"].is<int>()) {
        self->settingsMenu->setColorIndex(doc["eyeColorIndex"].as<int>());
    }
    if (doc["gmtOffsetHours"].is<int>()) {
        self->settingsMenu->setGmtOffsetHours(doc["gmtOffsetHours"].as<int>());
    }

    // Apply pomodoro settings
    if (self->pomodoroTimer) {
        if (doc["workMinutes"].is<int>()) {
            self->pomodoroTimer->setWorkMinutes(doc["workMinutes"].as<int>());
        }
        if (doc["shortBreakMinutes"].is<int>()) {
            self->pomodoroTimer->setShortBreakMinutes(doc["shortBreakMinutes"].as<int>());
        }
        if (doc["longBreakMinutes"].is<int>()) {
            self->pomodoroTimer->setLongBreakMinutes(doc["longBreakMinutes"].as<int>());
        }
        if (doc["sessionsBeforeLongBreak"].is<int>()) {
            self->pomodoroTimer->setSessionsBeforeLongBreak(doc["sessionsBeforeLongBreak"].as<int>());
        }
        if (doc["tickingEnabled"].is<bool>()) {
            self->pomodoroTimer->setTickingEnabled(doc["tickingEnabled"].as<bool>());
        }
    }

    self->settingsChanged = true;

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerManager::handleGetStatus(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    JsonDocument doc;
    self->buildStatusJson(doc);

    String json;
    serializeJson(doc, json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

esp_err_t WebServerManager::handleWiFiScan(httpd_req_t* req) {
    // Scan for networks (blocking, can take a few seconds)
    Serial.println("[WebServer] Starting WiFi scan...");
    int n = WiFi.scanNetworks();
    Serial.printf("[WebServer] Scan complete, found %d networks\n", n);

    JsonDocument doc;
    JsonArray networks = doc.to<JsonArray>();  // Return flat array, not wrapped

    for (int i = 0; i < n && i < 20; i++) {
        JsonObject net = networks.add<JsonObject>();
        net["ssid"] = WiFi.SSID(i);
        net["rssi"] = WiFi.RSSI(i);
        net["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }

    WiFi.scanDelete();

    String json;
    serializeJson(doc, json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

esp_err_t WebServerManager::handleWiFiConnect(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    // Read request body
    char content[256];
    int received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
        return ESP_FAIL;
    }
    content[received] = '\0';

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, content);
    if (error || !doc.containsKey("ssid") || !doc.containsKey("password")) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    String ssid = doc["ssid"].as<String>();
    String password = doc["password"].as<String>();

    // Save and connect
    if (self->wifiManager) {
        self->wifiManager->saveCredentials(ssid, password);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Connecting to new network...\"}");
    return ESP_OK;
}

esp_err_t WebServerManager::handlePomodoroStart(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    if (self->pomodoroTimer && !self->pomodoroTimer->isActive()) {
        self->pomodoroTimer->start();
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerManager::handlePomodoroStop(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    if (self->pomodoroTimer && self->pomodoroTimer->isActive()) {
        self->pomodoroTimer->stop();
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerManager::handleWiFiForget(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    if (self->wifiManager) {
        self->wifiManager->clearCredentials();
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"WiFi credentials cleared. Device will restart in AP mode.\"}");
    return ESP_OK;
}

esp_err_t WebServerManager::handleWiFiDisable(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    // Disable WiFi in settings - main loop will detect the change and call wifiManager.disable()
    if (self->settingsMenu) {
        self->settingsMenu->setWiFiEnabled(false);
        self->settingsChanged = true;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"WiFi will be disabled. Use device settings to re-enable.\"}");
    return ESP_OK;
}

esp_err_t WebServerManager::handleGetTime(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    JsonDocument doc;
    if (self->settingsMenu) {
        doc["hour"] = self->settingsMenu->getTimeHour();
        doc["minute"] = self->settingsMenu->getTimeMinute();
        doc["is24Hour"] = self->settingsMenu->is24HourFormat();
    }

    String json;
    serializeJson(doc, json);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

esp_err_t WebServerManager::handlePostTime(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    // Read request body
    char content[128];
    int received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
        return ESP_FAIL;
    }
    content[received] = '\0';

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, content);
    if (error) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Update time settings
    if (self->settingsMenu) {
        if (doc.containsKey("hour") && doc.containsKey("minute")) {
            int hour = doc["hour"].as<int>();
            int minute = doc["minute"].as<int>();
            self->settingsMenu->setTime(hour, minute);
        }
        if (doc.containsKey("is24Hour")) {
            self->settingsMenu->setTimeFormat(doc["is24Hour"].as<bool>());
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerManager::handlePostExpression(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    // Read request body
    char content[64];
    int received = httpd_req_recv(req, content, sizeof(content) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
        return ESP_FAIL;
    }
    content[received] = '\0';

    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, content);
    if (error) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    // Get expression index
    if (doc["index"].is<int>()) {
        int index = doc["index"].as<int>();
        if (self->expressionCallback && index >= 0 && index < 30) {
            self->expressionCallback(index);
            Serial.printf("[WebServer] Expression preview: %d\n", index);
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerManager::handleAudioTest(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    if (self->audioTestCallback) {
        self->audioTestCallback();
        Serial.println("[WebServer] Audio test triggered");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

// ============================================================================
// JSON Builders
// ============================================================================

void WebServerManager::buildSettingsJson(JsonDocument& doc) {
    if (!settingsMenu) return;

    JsonObject device = doc["device"].to<JsonObject>();
    device["volume"] = settingsMenu->getVolume();
    device["brightness"] = settingsMenu->getBrightness();
    device["micGain"] = settingsMenu->getMicSensitivity();
    device["micThreshold"] = settingsMenu->getMicThreshold();
    device["eyeColorIndex"] = settingsMenu->getColorIndex();
    device["timeFormat"] = settingsMenu->is24HourFormat() ? "24h" : "12h";
    device["gmtOffsetHours"] = settingsMenu->getGmtOffsetHours();

    if (pomodoroTimer) {
        JsonObject pomodoro = doc["pomodoro"].to<JsonObject>();
        pomodoro["workMinutes"] = pomodoroTimer->getWorkMinutes();
        pomodoro["shortBreakMinutes"] = pomodoroTimer->getShortBreakMinutes();
        pomodoro["longBreakMinutes"] = pomodoroTimer->getLongBreakMinutes();
        pomodoro["sessionsBeforeLongBreak"] = pomodoroTimer->getSessionsBeforeLongBreak();
        pomodoro["tickingEnabled"] = pomodoroTimer->isTickingEnabled();
    }
}

void WebServerManager::buildStatusJson(JsonDocument& doc) {
    // Settings version for change detection
    if (settingsMenu) {
        doc["settingsVersion"] = settingsMenu->getSettingsVersion();
    }

    // Uptime in seconds
    doc["uptimeSeconds"] = millis() / 1000;

    // Current mood/expression
    if (moodGetterCallback) {
        doc["currentMood"] = moodGetterCallback();
    }

    // Current time
    if (settingsMenu) {
        JsonObject time = doc["time"].to<JsonObject>();
        time["hour"] = settingsMenu->getTimeHour();
        time["minute"] = settingsMenu->getTimeMinute();
        time["is24Hour"] = settingsMenu->is24HourFormat();
        time["gmtOffsetHours"] = settingsMenu->getGmtOffsetHours();
        if (wifiManager) {
            time["ntpSynced"] = wifiManager->isNtpSynced();
        }
    }

    // WiFi status
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    if (wifiManager) {
        wifi["state"] = wifiManager->getStateString();
        wifi["connected"] = wifiManager->isConnected();
        wifi["ip"] = wifiManager->getIP().toString();
        if (wifiManager->isConnected()) {
            wifi["ssid"] = wifiManager->getSSID();
            wifi["rssi"] = wifiManager->getRSSI();
        }
    }

    // Pomodoro status
    if (pomodoroTimer) {
        JsonObject pomodoro = doc["pomodoro"].to<JsonObject>();
        pomodoro["active"] = pomodoroTimer->isActive();

        // Convert state enum to string
        const char* stateStr = "Idle";
        switch (pomodoroTimer->getState()) {
            case PomodoroState::Idle:         stateStr = "Idle"; break;
            case PomodoroState::Working:      stateStr = "Working"; break;
            case PomodoroState::ShortBreak:   stateStr = "Short Break"; break;
            case PomodoroState::LongBreak:    stateStr = "Long Break"; break;
            case PomodoroState::Celebration:  stateStr = "Celebration"; break;
            case PomodoroState::WaitingForTap: stateStr = "Waiting"; break;
        }
        pomodoro["state"] = stateStr;
        pomodoro["remainingSeconds"] = pomodoroTimer->getRemainingSeconds();
        pomodoro["currentSession"] = pomodoroTimer->getSessionNumber();
    }
}

// ============================================================================
// HTML Page Generation
// ============================================================================

String WebServerManager::generateSettingsPage() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>DeskBuddy</title>
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Inter:wght@300;400;500;600&family=JetBrains+Mono:wght@400;500&display=swap" rel="stylesheet">
    <style>
        :root {
            --background: #0A0A0A;
            --foreground: #F2F2F2;
            --card: #121212;
            --card-foreground: #F2F2F2;
            --primary: #DFFF00;
            --primary-foreground: #0A0A0A;
            --secondary: #1F1F1F;
            --muted: #262626;
            --muted-foreground: #999999;
            --border: #2E2E2E;
            --destructive: #EF4444;
            --status-active: #22C55E;
            --status-concept: #EAB308;
        }
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: 'Inter', -apple-system, BlinkMacSystemFont, sans-serif;
            background: var(--background);
            color: var(--foreground);
            line-height: 1.6;
            min-height: 100vh;
        }

        /* Navigation */
        .nav {
            position: sticky;
            top: 0;
            background: var(--background);
            border-bottom: 1px solid var(--border);
            z-index: 100;
            padding: 0 24px;
        }
        .nav-inner {
            max-width: 800px;
            margin: 0 auto;
            display: flex;
            align-items: center;
            justify-content: space-between;
            height: 60px;
        }
        .nav-brand {
            font-family: 'JetBrains Mono', monospace;
            font-weight: 600;
            font-size: 1.1em;
            color: var(--foreground);
            text-decoration: none;
        }
        .nav-status {
            display: flex;
            align-items: center;
            gap: 8px;
            font-family: 'JetBrains Mono', monospace;
            font-size: 0.75em;
            text-transform: uppercase;
            letter-spacing: 0.05em;
        }
        .status-dot {
            width: 8px;
            height: 8px;
            border-radius: 50%;
            background: var(--status-active);
            box-shadow: 0 0 8px var(--status-active);
        }
        .status-dot.disconnected {
            background: var(--destructive);
            box-shadow: 0 0 8px var(--destructive);
            animation: pulse 1s infinite;
        }
        @keyframes pulse { 0%, 100% { opacity: 1; } 50% { opacity: 0.4; } }

        /* Tabs */
        .tabs {
            display: flex;
            gap: 4px;
            padding: 16px 24px;
            background: var(--background);
            border-bottom: 1px solid var(--border);
            overflow-x: auto;
            max-width: 800px;
            margin: 0 auto;
        }
        .tab {
            font-family: 'JetBrains Mono', monospace;
            font-size: 0.8em;
            text-transform: uppercase;
            letter-spacing: 0.05em;
            padding: 8px 16px;
            background: transparent;
            border: 1px solid var(--border);
            border-radius: 6px;
            color: var(--muted-foreground);
            cursor: pointer;
            transition: all 0.2s;
            white-space: nowrap;
        }
        .tab:hover { border-color: var(--muted-foreground); color: var(--foreground); }
        .tab.active {
            background: var(--primary);
            color: var(--primary-foreground);
            border-color: var(--primary);
        }

        /* Main content */
        .container {
            max-width: 800px;
            margin: 0 auto;
            padding: 32px 24px;
        }
        .section { display: none; }
        .section.active { display: block; }

        /* Section headers */
        .section-header {
            font-family: 'JetBrains Mono', monospace;
            font-size: 0.75em;
            text-transform: uppercase;
            letter-spacing: 0.1em;
            color: var(--muted-foreground);
            margin-bottom: 24px;
            padding-bottom: 8px;
            border-bottom: 2px solid var(--primary);
            display: inline-block;
        }

        /* Cards */
        .card {
            background: var(--card);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 24px;
            margin-bottom: 24px;
        }
        .card-title {
            font-size: 1em;
            font-weight: 600;
            margin-bottom: 16px;
            color: var(--foreground);
        }

        /* Dashboard grid */
        .dashboard-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 16px;
            margin-bottom: 32px;
        }
        .stat-card {
            background: var(--card);
            border: 1px solid var(--border);
            border-radius: 8px;
            padding: 20px;
        }
        .stat-label {
            font-family: 'JetBrains Mono', monospace;
            font-size: 0.7em;
            text-transform: uppercase;
            letter-spacing: 0.05em;
            color: var(--muted-foreground);
            margin-bottom: 8px;
        }
        .stat-value {
            font-size: 1.5em;
            font-weight: 600;
            color: var(--foreground);
        }
        .stat-value.accent { color: var(--primary); }

        /* Eye color preview */
        .eye-preview {
            display: flex;
            align-items: center;
            gap: 16px;
        }
        .eye-dot {
            width: 40px;
            height: 40px;
            border-radius: 50%;
            border: 2px solid var(--border);
        }

        /* Pomodoro display */
        .pomodoro-display {
            text-align: center;
            padding: 32px;
        }
        .pomodoro-time {
            font-family: 'JetBrains Mono', monospace;
            font-size: 4em;
            font-weight: 600;
            color: var(--primary);
            margin-bottom: 8px;
        }
        .pomodoro-state {
            font-family: 'JetBrains Mono', monospace;
            font-size: 0.9em;
            text-transform: uppercase;
            letter-spacing: 0.1em;
            color: var(--muted-foreground);
        }

        /* Form elements */
        .form-group {
            margin-bottom: 20px;
        }
        .form-label {
            display: flex;
            justify-content: space-between;
            align-items: center;
            font-size: 0.9em;
            color: var(--muted-foreground);
            margin-bottom: 8px;
        }
        .form-value {
            font-family: 'JetBrains Mono', monospace;
            color: var(--primary);
        }
        input[type="range"] {
            width: 100%;
            height: 6px;
            -webkit-appearance: none;
            background: var(--muted);
            border-radius: 3px;
            outline: none;
        }
        input[type="range"]::-webkit-slider-thumb {
            -webkit-appearance: none;
            width: 18px;
            height: 18px;
            background: var(--primary);
            border-radius: 50%;
            cursor: pointer;
        }
        select {
            background: var(--secondary);
            color: var(--foreground);
            border: 1px solid var(--border);
            padding: 10px 14px;
            border-radius: 6px;
            font-family: 'JetBrains Mono', monospace;
            font-size: 0.9em;
            cursor: pointer;
        }
        select:focus { border-color: var(--primary); outline: none; }
        .time-row {
            display: flex;
            gap: 12px;
            align-items: center;
        }
        .time-row select { flex: 1; }
        .time-row span { color: var(--muted-foreground); font-size: 1.5em; }

        /* Toggle */
        .toggle-row {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 12px 0;
            border-bottom: 1px solid var(--border);
        }
        .toggle-row:last-child { border-bottom: none; }
        .toggle-label { color: var(--foreground); font-size: 0.95em; }
        .toggle {
            position: relative;
            width: 48px;
            height: 26px;
        }
        .toggle input { opacity: 0; width: 0; height: 0; }
        .toggle .slider {
            position: absolute;
            cursor: pointer;
            inset: 0;
            background: var(--muted);
            border-radius: 26px;
            transition: 0.3s;
        }
        .toggle .slider:before {
            position: absolute;
            content: "";
            height: 20px;
            width: 20px;
            left: 3px;
            bottom: 3px;
            background: var(--muted-foreground);
            border-radius: 50%;
            transition: 0.3s;
        }
        .toggle input:checked + .slider { background: var(--primary); }
        .toggle input:checked + .slider:before {
            transform: translateX(22px);
            background: var(--primary-foreground);
        }

        /* Buttons */
        .btn {
            font-family: 'JetBrains Mono', monospace;
            font-size: 0.85em;
            font-weight: 500;
            text-transform: uppercase;
            letter-spacing: 0.05em;
            padding: 12px 24px;
            border-radius: 6px;
            border: none;
            cursor: pointer;
            transition: all 0.2s;
            width: 100%;
        }
        .btn-primary {
            background: var(--primary);
            color: var(--primary-foreground);
        }
        .btn-primary:hover { filter: brightness(0.9); }
        .btn-secondary {
            background: var(--secondary);
            color: var(--foreground);
            border: 1px solid var(--border);
        }
        .btn-secondary:hover { border-color: var(--muted-foreground); }
        .btn-danger {
            background: var(--destructive);
            color: white;
        }
        .btn-danger:hover { filter: brightness(0.9); }
        .btn + .btn { margin-top: 12px; }

        /* WiFi list */
        .wifi-list {
            max-height: 240px;
            overflow-y: auto;
            margin-bottom: 16px;
        }
        .wifi-network {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 12px;
            background: var(--secondary);
            border: 1px solid var(--border);
            border-radius: 6px;
            margin-bottom: 8px;
            cursor: pointer;
            transition: all 0.2s;
        }
        .wifi-network:hover { border-color: var(--primary); }
        .wifi-ssid { font-weight: 500; }
        .wifi-signal {
            font-family: 'JetBrains Mono', monospace;
            font-size: 0.8em;
            color: var(--muted-foreground);
        }
        .wifi-input {
            width: 100%;
            padding: 12px 14px;
            background: var(--secondary);
            border: 1px solid var(--border);
            border-radius: 6px;
            color: var(--foreground);
            font-size: 0.95em;
            margin-bottom: 12px;
        }
        .wifi-input:focus { border-color: var(--primary); outline: none; }
        .hidden { display: none !important; }

        /* Status row */
        .status-row {
            display: flex;
            justify-content: space-between;
            padding: 12px 0;
            border-bottom: 1px solid var(--border);
        }
        .status-row:last-child { border-bottom: none; }
        .status-row-label { color: var(--muted-foreground); }
        .status-row-value {
            font-family: 'JetBrains Mono', monospace;
            color: var(--foreground);
        }

        /* Color grid */
        .color-grid {
            display: grid;
            grid-template-columns: repeat(4, 1fr);
            gap: 12px;
        }
        .color-swatch {
            aspect-ratio: 1;
            border-radius: 8px;
            border: 2px solid var(--border);
            cursor: pointer;
            transition: all 0.2s;
            display: flex;
            align-items: center;
            justify-content: center;
            font-family: 'JetBrains Mono', monospace;
            font-size: 0.65em;
            text-transform: uppercase;
            color: transparent;
        }
        .color-swatch:hover {
            border-color: var(--foreground);
            transform: scale(1.05);
        }
        .color-swatch:hover { color: var(--background); text-shadow: 0 0 4px var(--foreground); }
        .color-swatch.active {
            border-color: var(--primary);
            border-width: 3px;
            box-shadow: 0 0 12px var(--primary);
        }

        /* Expression grid */
        .expr-grid {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(120px, 1fr));
            gap: 12px;
        }
        .expr-btn {
            padding: 16px 12px;
            background: var(--secondary);
            border: 1px solid var(--border);
            border-radius: 8px;
            color: var(--foreground);
            font-family: 'JetBrains Mono', monospace;
            font-size: 0.75em;
            text-transform: uppercase;
            letter-spacing: 0.03em;
            cursor: pointer;
            transition: all 0.2s;
            text-align: center;
        }
        .expr-btn:hover {
            border-color: var(--primary);
            background: var(--muted);
        }
        .expr-btn:active, .expr-btn.active {
            background: var(--primary);
            color: var(--primary-foreground);
            border-color: var(--primary);
        }

        /* Toast */
        .toast {
            position: fixed;
            bottom: 24px;
            left: 50%;
            transform: translateX(-50%);
            padding: 12px 24px;
            border-radius: 6px;
            font-family: 'JetBrains Mono', monospace;
            font-size: 0.85em;
            z-index: 200;
            animation: slideUp 0.3s ease;
        }
        .toast.success { background: var(--status-active); color: var(--background); }
        .toast.error { background: var(--destructive); color: white; }
        @keyframes slideUp {
            from { transform: translateX(-50%) translateY(20px); opacity: 0; }
            to { transform: translateX(-50%) translateY(0); opacity: 1; }
        }
    </style>
</head>
<body>
    <nav class="nav">
        <div class="nav-inner">
            <span class="nav-brand">DeskBuddy</span>
            <div class="nav-status">
                <span class="status-dot" id="conn-dot"></span>
                <span id="conn-text">Connected</span>
            </div>
        </div>
    </nav>

    <div class="tabs">
        <button class="tab active" data-tab="dashboard">Dashboard</button>
        <button class="tab" data-tab="display">Display</button>
        <button class="tab" data-tab="audio">Audio</button>
        <button class="tab" data-tab="time">Time</button>
        <button class="tab" data-tab="wifi">WiFi</button>
        <button class="tab" data-tab="pomodoro">Pomodoro</button>
        <button class="tab" data-tab="expressions">Expressions</button>
    </div>

    <main class="container">
        <!-- Dashboard -->
        <section id="dashboard" class="section active">
            <span class="section-header">Overview</span>

            <div class="dashboard-grid">
                <div class="stat-card">
                    <div class="stat-label">Status</div>
                    <div class="stat-value" id="dash-status">Online</div>
                </div>
                <div class="stat-card">
                    <div class="stat-label">WiFi</div>
                    <div class="stat-value" id="dash-wifi">--</div>
                </div>
                <div class="stat-card">
                    <div class="stat-label">IP Address</div>
                    <div class="stat-value" id="dash-ip">--</div>
                </div>
                <div class="stat-card">
                    <div class="stat-label">Current Mood</div>
                    <div class="stat-value accent" id="dash-mood">Neutral</div>
                </div>
                <div class="stat-card">
                    <div class="stat-label">Current Time</div>
                    <div class="stat-value" id="dash-time">--:--</div>
                </div>
                <div class="stat-card">
                    <div class="stat-label">Uptime</div>
                    <div class="stat-value" id="dash-uptime">--</div>
                </div>
            </div>

            <div class="card">
                <div class="card-title">Eye Color</div>
                <div class="eye-preview">
                    <div class="eye-dot" id="eye-color-dot"></div>
                    <span id="eye-color-name">Cyan</span>
                </div>
            </div>

            <div class="card">
                <div class="card-title">Quick Settings</div>
                <div class="form-group">
                    <div class="form-label">
                        <span>Volume</span>
                        <span class="form-value" id="dash-volume-val">50%</span>
                    </div>
                    <input type="range" id="dash-volume" min="0" max="100" value="50">
                </div>
                <div class="form-group">
                    <div class="form-label">
                        <span>Brightness</span>
                        <span class="form-value" id="dash-brightness-val">100%</span>
                    </div>
                    <input type="range" id="dash-brightness" min="0" max="100" value="100">
                </div>
            </div>
        </section>

        <!-- Display Settings -->
        <section id="display" class="section">
            <span class="section-header">Display</span>
            <div class="card">
                <div class="form-group">
                    <div class="form-label">
                        <span>Brightness</span>
                        <span class="form-value" id="brightness-val">100%</span>
                    </div>
                    <input type="range" id="brightness" min="0" max="100" value="100">
                </div>
            </div>
            <div class="card">
                <div class="card-title">Eye Color</div>
                <div class="color-grid" id="color-grid"></div>
            </div>
        </section>

        <!-- Audio Settings -->
        <section id="audio" class="section">
            <span class="section-header">Audio</span>
            <div class="card">
                <div class="form-group">
                    <div class="form-label">
                        <span>Volume</span>
                        <span class="form-value" id="volume-val">50%</span>
                    </div>
                    <input type="range" id="volume" min="0" max="100" value="50">
                </div>
                <div class="form-group">
                    <div class="form-label">
                        <span>Microphone Gain</span>
                        <span class="form-value" id="micGain-val">50%</span>
                    </div>
                    <input type="range" id="micGain" min="0" max="100" value="50">
                </div>
                <div class="form-group">
                    <div class="form-label">
                        <span>Mic Threshold</span>
                        <span class="form-value" id="micThreshold-val">50%</span>
                    </div>
                    <input type="range" id="micThreshold" min="0" max="100" value="50">
                </div>
                <button class="btn btn-secondary" onclick="testAudio()" style="margin-top: 16px;">Test Audio</button>
            </div>
        </section>

        <!-- Time Settings -->
        <section id="time" class="section">
            <span class="section-header">Time</span>
            <div class="card">
                <div class="status-row">
                    <span class="status-row-label">NTP Sync</span>
                    <span class="status-row-value" id="ntp-status">--</span>
                </div>
                <div class="form-group">
                    <div class="form-label"><span>Timezone (UTC)</span></div>
                    <select id="timezone-select" class="wifi-input" onchange="setTimezone()">
                        <option value="-12">UTC-12</option>
                        <option value="-11">UTC-11</option>
                        <option value="-10">UTC-10 (Hawaii)</option>
                        <option value="-9">UTC-9 (Alaska)</option>
                        <option value="-8">UTC-8 (Pacific)</option>
                        <option value="-7">UTC-7 (Mountain)</option>
                        <option value="-6">UTC-6 (Central)</option>
                        <option value="-5">UTC-5 (Eastern)</option>
                        <option value="-4">UTC-4 (Atlantic)</option>
                        <option value="-3">UTC-3</option>
                        <option value="-2">UTC-2</option>
                        <option value="-1">UTC-1</option>
                        <option value="0" selected>UTC+0 (GMT)</option>
                        <option value="1">UTC+1 (CET)</option>
                        <option value="2">UTC+2 (EET)</option>
                        <option value="3">UTC+3 (Moscow)</option>
                        <option value="4">UTC+4</option>
                        <option value="5">UTC+5</option>
                        <option value="5.5">UTC+5:30 (India)</option>
                        <option value="6">UTC+6</option>
                        <option value="7">UTC+7</option>
                        <option value="8">UTC+8 (China)</option>
                        <option value="9">UTC+9 (Japan)</option>
                        <option value="10">UTC+10 (Sydney)</option>
                        <option value="11">UTC+11</option>
                        <option value="12">UTC+12</option>
                        <option value="13">UTC+13</option>
                        <option value="14">UTC+14</option>
                    </select>
                </div>
                <div class="form-group">
                    <div class="form-label"><span>Manual Time (offline mode)</span></div>
                    <div class="time-row">
                        <select id="time-hour"></select>
                        <span>:</span>
                        <select id="time-minute"></select>
                    </div>
                </div>
                <div class="toggle-row">
                    <span class="toggle-label">24-hour format</span>
                    <label class="toggle">
                        <input type="checkbox" id="time-24h">
                        <span class="slider"></span>
                    </label>
                </div>
            </div>
        </section>

        <!-- WiFi Settings -->
        <section id="wifi" class="section">
            <span class="section-header">WiFi</span>
            <div class="card">
                <div class="status-row">
                    <span class="status-row-label">Network</span>
                    <span class="status-row-value" id="wifi-ssid">--</span>
                </div>
                <div class="status-row">
                    <span class="status-row-label">Signal</span>
                    <span class="status-row-value" id="wifi-rssi">--</span>
                </div>
                <div class="status-row">
                    <span class="status-row-label">IP Address</span>
                    <span class="status-row-value" id="wifi-ip">--</span>
                </div>
            </div>

            <div class="card">
                <div class="card-title">Available Networks</div>
                <div class="wifi-list" id="wifi-list"></div>
                <button class="btn btn-secondary" onclick="scanWiFi()">Scan Networks</button>
                <div id="wifi-connect-form" class="hidden" style="margin-top: 16px;">
                    <input type="text" id="wifi-ssid-input" class="wifi-input" placeholder="Network name">
                    <input type="password" id="wifi-pass-input" class="wifi-input" placeholder="Password">
                    <button class="btn btn-primary" onclick="connectWiFi()">Connect</button>
                </div>
            </div>

            <div class="card">
                <div class="card-title">Danger Zone</div>
                <div style="display: flex; gap: 8px;">
                    <button class="btn btn-danger" style="flex: 1 1 0; min-width: 0;" onclick="forgetWiFi()">Forget Network</button>
                    <button class="btn btn-danger" style="flex: 1 1 0; min-width: 0; margin-top: 0;" onclick="disableWiFi()">Disable WiFi</button>
                </div>
                <p style="margin-top: 12px; font-size: 12px; color: #888;">Disabling WiFi will disconnect this page. Use device settings to re-enable.</p>
            </div>
        </section>

        <!-- Pomodoro -->
        <section id="pomodoro" class="section">
            <span class="section-header">Pomodoro Timer</span>
            <div class="card">
                <div class="pomodoro-display">
                    <div class="pomodoro-time" id="pomo-time">--:--</div>
                    <div class="pomodoro-state" id="pomo-state">Ready</div>
                </div>
                <button class="btn btn-primary" id="btn-start" onclick="startPomodoro()">Start</button>
                <button class="btn btn-danger hidden" id="btn-stop" onclick="stopPomodoro()">Stop</button>
            </div>

            <div class="card">
                <div class="card-title">Durations</div>
                <div class="form-group">
                    <div class="form-label">
                        <span>Work Duration</span>
                        <span class="form-value" id="workMinutes-val">25 min</span>
                    </div>
                    <input type="range" id="workMinutes" min="1" max="60" value="25">
                </div>
                <div class="form-group">
                    <div class="form-label">
                        <span>Short Break</span>
                        <span class="form-value" id="shortBreakMinutes-val">5 min</span>
                    </div>
                    <input type="range" id="shortBreakMinutes" min="1" max="30" value="5">
                </div>
                <div class="form-group">
                    <div class="form-label">
                        <span>Long Break</span>
                        <span class="form-value" id="longBreakMinutes-val">15 min</span>
                    </div>
                    <input type="range" id="longBreakMinutes" min="5" max="60" value="15">
                </div>
                <div class="form-group">
                    <div class="form-label">
                        <span>Sessions Before Long Break</span>
                        <span class="form-value" id="sessionsBeforeLongBreak-val">4</span>
                    </div>
                    <input type="range" id="sessionsBeforeLongBreak" min="1" max="8" value="4">
                </div>
            </div>

            <div class="card">
                <div class="card-title">Options</div>
                <div class="toggle-row">
                    <span class="toggle-label">Ticking Sound (last 60s)</span>
                    <label class="toggle">
                        <input type="checkbox" id="tickingEnabled" checked>
                        <span class="slider"></span>
                    </label>
                </div>
            </div>
        </section>

        <!-- Expressions -->
        <section id="expressions" class="section">
            <span class="section-header">Expression Preview</span>
            <div class="card">
                <div class="status-row">
                    <span class="status-row-label">Current Mood</span>
                    <span class="status-row-value accent" id="expr-current-mood">Neutral</span>
                </div>
            </div>
            <div class="card">
                <div class="card-title">Click to preview on device</div>
                <div class="expr-grid" id="expr-grid"></div>
            </div>
        </section>
    </main>

    <script>
        // Color presets matching device
        // Colors matching device COLOR_PRESETS order
        const EYE_COLORS = [
            { name: 'Cyan', hex: '#00FFFF' },
            { name: 'Pink', hex: '#FF00FF' },
            { name: 'Green', hex: '#00FF00' },
            { name: 'Orange', hex: '#FFA500' },
            { name: 'Purple', hex: '#8000FF' },
            { name: 'White', hex: '#FFFFFF' },
            { name: 'Red', hex: '#FF0000' },
            { name: 'Blue', hex: '#0000FF' }
        ];

        let lastSettingsVersion = -1;
        let failCount = 0;

        // Tab navigation
        document.querySelectorAll('.tab').forEach(tab => {
            tab.addEventListener('click', () => {
                document.querySelectorAll('.tab').forEach(t => t.classList.remove('active'));
                document.querySelectorAll('.section').forEach(s => s.classList.remove('active'));
                tab.classList.add('active');
                document.getElementById(tab.dataset.tab).classList.add('active');
            });
        });

        function showToast(msg, type = 'success') {
            document.querySelectorAll('.toast').forEach(t => t.remove());
            const toast = document.createElement('div');
            toast.className = 'toast ' + type;
            toast.textContent = msg;
            document.body.appendChild(toast);
            setTimeout(() => toast.remove(), 3000);
        }

        async function testAudio() {
            try {
                await fetch('/api/audio/test', { method: 'POST' });
                showToast('Playing test sound');
            } catch (e) {
                showToast('Audio test failed', 'error');
            }
        }

        function setConnected(connected) {
            const dot = document.getElementById('conn-dot');
            const text = document.getElementById('conn-text');
            if (connected) {
                dot.classList.remove('disconnected');
                text.textContent = 'Connected';
            } else {
                dot.classList.add('disconnected');
                text.textContent = 'Offline';
            }
        }

        async function loadData() {
            try {
                const status = await fetch('/api/status').then(r => r.json());
                if (failCount > 0) { setConnected(true); failCount = 0; }

                // WiFi status
                if (status.wifi) {
                    const ssid = status.wifi.connected ? status.wifi.ssid : status.wifi.state;
                    document.getElementById('dash-wifi').textContent = ssid;
                    document.getElementById('dash-ip').textContent = status.wifi.ip || '--';
                    document.getElementById('wifi-ssid').textContent = status.wifi.connected ? status.wifi.ssid : 'Not connected';
                    document.getElementById('wifi-rssi').textContent = status.wifi.rssi ? status.wifi.rssi + ' dBm' : '--';
                    document.getElementById('wifi-ip').textContent = status.wifi.ip || '--';
                }

                // Pomodoro
                if (status.pomodoro) {
                    updatePomodoroUI(status.pomodoro);
                }

                // Current time
                if (status.time) {
                    const h = status.time.hour;
                    const m = status.time.minute;
                    let timeStr;
                    if (status.time.is24Hour) {
                        timeStr = h.toString().padStart(2, '0') + ':' + m.toString().padStart(2, '0');
                    } else {
                        const h12 = h % 12 || 12;
                        const ampm = h < 12 ? 'AM' : 'PM';
                        timeStr = h12 + ':' + m.toString().padStart(2, '0') + ' ' + ampm;
                    }
                    document.getElementById('dash-time').textContent = timeStr;

                    // NTP status
                    const ntpEl = document.getElementById('ntp-status');
                    if (ntpEl) {
                        ntpEl.textContent = status.time.ntpSynced ? 'Synced' : 'Not synced';
                        ntpEl.style.color = status.time.ntpSynced ? '#DFFF00' : '#888';
                    }
                }

                // Uptime
                if (status.uptimeSeconds !== undefined) {
                    const secs = status.uptimeSeconds;
                    const days = Math.floor(secs / 86400);
                    const hrs = Math.floor((secs % 86400) / 3600);
                    const mins = Math.floor((secs % 3600) / 60);
                    let uptimeStr;
                    if (days > 0) {
                        uptimeStr = days + 'd ' + hrs + 'h';
                    } else if (hrs > 0) {
                        uptimeStr = hrs + 'h ' + mins + 'm';
                    } else {
                        uptimeStr = mins + 'm';
                    }
                    document.getElementById('dash-uptime').textContent = uptimeStr;
                }

                // Current mood - update dashboard and expressions page
                if (status.currentMood) {
                    document.getElementById('dash-mood').textContent = status.currentMood;
                    document.getElementById('expr-current-mood').textContent = status.currentMood;
                }

                // Check settings version
                const ver = status.settingsVersion || 0;
                if (ver !== lastSettingsVersion) {
                    lastSettingsVersion = ver;
                    await loadSettings();
                }
            } catch (e) {
                failCount++;
                if (failCount >= 3) setConnected(false);
            }
        }

        async function loadSettings() {
            try {
                const [settings, time] = await Promise.all([
                    fetch('/api/settings').then(r => r.json()),
                    fetch('/api/time').then(r => r.json())
                ]);

                if (settings.device) {
                    // Update all sliders (including dashboard duplicates)
                    setSlider('volume', settings.device.volume);
                    setSlider('brightness', settings.device.brightness);
                    setSlider('micGain', settings.device.micGain);
                    setSlider('micThreshold', settings.device.micThreshold);
                    setSlider('dash-volume', settings.device.volume);
                    setSlider('dash-brightness', settings.device.brightness);

                    // Eye color - update dashboard and color grid
                    const colorIdx = settings.device.eyeColorIndex || 0;
                    const color = EYE_COLORS[colorIdx] || EYE_COLORS[0];
                    document.getElementById('eye-color-dot').style.background = color.hex;
                    document.getElementById('eye-color-name').textContent = color.name;
                    selectColor(colorIdx);
                }

                if (settings.pomodoro) {
                    // Pomodoro sliders
                    setPomoSlider('workMinutes', settings.pomodoro.workMinutes, ' min');
                    setPomoSlider('shortBreakMinutes', settings.pomodoro.shortBreakMinutes, ' min');
                    setPomoSlider('longBreakMinutes', settings.pomodoro.longBreakMinutes, ' min');
                    setPomoSlider('sessionsBeforeLongBreak', settings.pomodoro.sessionsBeforeLongBreak, '');
                    document.getElementById('tickingEnabled').checked = settings.pomodoro.tickingEnabled;
                }

                if (time) {
                    document.getElementById('time-hour').value = time.hour;
                    document.getElementById('time-minute').value = time.minute;
                    document.getElementById('time-24h').checked = time.is24Hour;
                    if (time.gmtOffsetHours !== undefined) {
                        document.getElementById('timezone-select').value = time.gmtOffsetHours;
                    }
                }

                // Also load timezone from device settings
                if (settings.device && settings.device.gmtOffsetHours !== undefined) {
                    document.getElementById('timezone-select').value = settings.device.gmtOffsetHours;
                }
            } catch (e) {
                console.error('Failed to load settings:', e);
            }
        }

        function setSlider(id, value) {
            const el = document.getElementById(id);
            const val = document.getElementById(id + '-val');
            if (el) el.value = value;
            if (val) val.textContent = value + '%';
        }

        function setPomoSlider(id, value, suffix) {
            const el = document.getElementById(id);
            const val = document.getElementById(id + '-val');
            if (el) el.value = value;
            if (val) val.textContent = value + suffix;
        }

        function selectColor(idx) {
            document.querySelectorAll('.color-swatch').forEach((s, i) => {
                s.classList.toggle('active', i === idx);
            });
        }

        function updatePomodoroUI(pomo) {
            const timeEl = document.getElementById('pomo-time');
            const stateEl = document.getElementById('pomo-state');
            const startBtn = document.getElementById('btn-start');
            const stopBtn = document.getElementById('btn-stop');

            if (pomo.active) {
                const m = Math.floor(pomo.remainingSeconds / 60);
                const s = pomo.remainingSeconds % 60;
                timeEl.textContent = m + ':' + s.toString().padStart(2, '0');
                stateEl.textContent = pomo.state;
                startBtn.classList.add('hidden');
                stopBtn.classList.remove('hidden');
            } else {
                timeEl.textContent = '--:--';
                stateEl.textContent = 'Ready';
                startBtn.classList.remove('hidden');
                stopBtn.classList.add('hidden');
            }
        }

        // Setting updates
        let updateTimeout;
        function updateSetting(key, value) {
            clearTimeout(updateTimeout);
            updateTimeout = setTimeout(() => {
                fetch('/api/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ [key]: parseInt(value) })
                });
            }, 300);
        }

        // Slider listeners
        ['volume', 'brightness', 'micGain', 'micThreshold'].forEach(key => {
            const el = document.getElementById(key);
            if (el) el.addEventListener('input', (e) => {
                document.getElementById(key + '-val').textContent = e.target.value + '%';
                updateSetting(key, e.target.value);
            });
        });

        // Dashboard quick sliders
        document.getElementById('dash-volume').addEventListener('input', (e) => {
            document.getElementById('dash-volume-val').textContent = e.target.value + '%';
            document.getElementById('volume').value = e.target.value;
            document.getElementById('volume-val').textContent = e.target.value + '%';
            updateSetting('volume', e.target.value);
        });
        document.getElementById('dash-brightness').addEventListener('input', (e) => {
            document.getElementById('dash-brightness-val').textContent = e.target.value + '%';
            document.getElementById('brightness').value = e.target.value;
            document.getElementById('brightness-val').textContent = e.target.value + '%';
            updateSetting('brightness', e.target.value);
        });

        // Timezone setting
        function setTimezone() {
            const tz = document.getElementById('timezone-select').value;
            updateSetting('gmtOffsetHours', tz);
            showToast('Timezone updated - NTP will re-sync');
        }

        // Pomodoro
        async function startPomodoro() {
            try {
                await fetch('/api/pomodoro/start', { method: 'POST' });
                showToast('Pomodoro started');
                loadData();
            } catch (e) { showToast('Failed to start', 'error'); }
        }

        async function stopPomodoro() {
            try {
                await fetch('/api/pomodoro/stop', { method: 'POST' });
                showToast('Pomodoro stopped');
                loadData();
            } catch (e) { showToast('Failed to stop', 'error'); }
        }

        // Time dropdowns
        const hourSel = document.getElementById('time-hour');
        const minSel = document.getElementById('time-minute');
        for (let i = 0; i < 24; i++) {
            const opt = document.createElement('option');
            opt.value = i;
            opt.textContent = i.toString().padStart(2, '0');
            hourSel.appendChild(opt);
        }
        for (let i = 0; i < 60; i++) {
            const opt = document.createElement('option');
            opt.value = i;
            opt.textContent = i.toString().padStart(2, '0');
            minSel.appendChild(opt);
        }

        function updateTime() {
            fetch('/api/time', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({
                    hour: parseInt(hourSel.value),
                    minute: parseInt(minSel.value),
                    is24Hour: document.getElementById('time-24h').checked
                })
            });
        }
        hourSel.addEventListener('change', updateTime);
        minSel.addEventListener('change', updateTime);
        document.getElementById('time-24h').addEventListener('change', updateTime);

        // WiFi
        async function scanWiFi() {
            const list = document.getElementById('wifi-list');
            list.innerHTML = '<div style="text-align:center;padding:20px;color:var(--muted-foreground)">Scanning...</div>';
            try {
                const networks = await fetch('/api/wifi/scan').then(r => r.json());
                list.innerHTML = '';
                if (networks.length === 0) {
                    list.innerHTML = '<div style="text-align:center;padding:16px;color:var(--muted-foreground)">No networks found</div>';
                    return;
                }
                networks.forEach(net => {
                    const div = document.createElement('div');
                    div.className = 'wifi-network';
                    div.innerHTML = '<span class="wifi-ssid">' + net.ssid + '</span><span class="wifi-signal">' + net.rssi + ' dBm</span>';
                    div.onclick = () => selectNetwork(net.ssid);
                    list.appendChild(div);
                });
            } catch (e) {
                list.innerHTML = '<div style="text-align:center;padding:16px;color:var(--destructive)">Scan failed</div>';
            }
        }

        function selectNetwork(ssid) {
            document.getElementById('wifi-ssid-input').value = ssid;
            document.getElementById('wifi-pass-input').value = '';
            document.getElementById('wifi-connect-form').classList.remove('hidden');
        }

        async function connectWiFi() {
            const ssid = document.getElementById('wifi-ssid-input').value;
            const pass = document.getElementById('wifi-pass-input').value;
            if (!ssid) return showToast('Enter network name', 'error');
            try {
                await fetch('/api/wifi/connect', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ ssid, password: pass })
                });
                showToast('Connecting to ' + ssid);
                document.getElementById('wifi-connect-form').classList.add('hidden');
            } catch (e) { showToast('Connection failed', 'error'); }
        }

        async function forgetWiFi() {
            if (!confirm('Clear WiFi and enter setup mode?')) return;
            try {
                await fetch('/api/wifi/forget', { method: 'POST' });
                showToast('WiFi cleared');
            } catch (e) { showToast('Failed', 'error'); }
        }

        async function disableWiFi() {
            if (!confirm('Disable WiFi completely?\\n\\nThis will disconnect this page immediately.\\nUse the device settings menu to re-enable WiFi.')) return;
            try {
                await fetch('/api/wifi/disable', { method: 'POST' });
                showToast('WiFi disabled');
            } catch (e) { showToast('Failed', 'error'); }
        }

        // Expression names (matching Expression enum order)
        const EXPRESSIONS = [
            'Neutral', 'Happy', 'Sad', 'Surprised', 'Angry', 'Suspicious',
            'Sleepy', 'Scared', 'Content', 'Startled', 'Grumpy', 'Joyful',
            'Focused', 'Confused', 'Yawn', 'Petting', 'Dazed', 'Dizzy',
            'Love', 'Joy', 'Curious', 'Thinking', 'Mischievous', 'Bored',
            'Alert', 'Smug', 'Dreamy', 'Skeptical', 'Squint', 'Wink'
        ];

        // Populate color grid
        const colorGrid = document.getElementById('color-grid');
        EYE_COLORS.forEach((color, idx) => {
            const swatch = document.createElement('div');
            swatch.className = 'color-swatch';
            swatch.style.background = color.hex;
            swatch.textContent = color.name;
            swatch.onclick = () => setEyeColor(idx);
            colorGrid.appendChild(swatch);
        });

        async function setEyeColor(idx) {
            selectColor(idx);
            const color = EYE_COLORS[idx];
            document.getElementById('eye-color-dot').style.background = color.hex;
            document.getElementById('eye-color-name').textContent = color.name;
            try {
                await fetch('/api/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ eyeColorIndex: idx })
                });
                showToast(color.name);
            } catch (e) {
                showToast('Failed to set color', 'error');
            }
        }

        // Pomodoro slider listeners
        ['workMinutes', 'shortBreakMinutes', 'longBreakMinutes'].forEach(key => {
            const el = document.getElementById(key);
            if (el) el.addEventListener('input', (e) => {
                document.getElementById(key + '-val').textContent = e.target.value + ' min';
                updatePomoSetting(key, parseInt(e.target.value));
            });
        });
        const sessEl = document.getElementById('sessionsBeforeLongBreak');
        if (sessEl) sessEl.addEventListener('input', (e) => {
            document.getElementById('sessionsBeforeLongBreak-val').textContent = e.target.value;
            updatePomoSetting('sessionsBeforeLongBreak', parseInt(e.target.value));
        });
        const tickEl = document.getElementById('tickingEnabled');
        if (tickEl) tickEl.addEventListener('change', (e) => {
            updatePomoSetting('tickingEnabled', e.target.checked);
        });

        let pomoUpdateTimeout;
        function updatePomoSetting(key, value) {
            clearTimeout(pomoUpdateTimeout);
            pomoUpdateTimeout = setTimeout(() => {
                fetch('/api/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ [key]: value })
                });
            }, 300);
        }

        // Populate expression grid
        const exprGrid = document.getElementById('expr-grid');
        EXPRESSIONS.forEach((name, idx) => {
            const btn = document.createElement('button');
            btn.className = 'expr-btn';
            btn.textContent = name;
            btn.onclick = () => previewExpression(idx, btn);
            exprGrid.appendChild(btn);
        });

        async function previewExpression(index, btn) {
            // Visual feedback
            document.querySelectorAll('.expr-btn').forEach(b => b.classList.remove('active'));
            btn.classList.add('active');
            try {
                await fetch('/api/expression', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ index })
                });
                showToast(EXPRESSIONS[index]);
            } catch (e) {
                showToast('Failed to preview', 'error');
            }
        }

        // Init
        loadData();
        setInterval(loadData, 1000);
    </script>
</body>
</html>
)rawliteral";

    return html;
}
