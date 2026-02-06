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
#include "ota_manager.h"
#include "../ui/settings_menu.h"
#include "../ui/pomodoro.h"
#include "../ui/countdown_timer.h"
#include "../ui/reminder_manager.h"
#include "../behavior/breathing_exercise.h"
#include "../assistant/mcp_client.h"
#include "../assistant/mcp_server.h"
#include "../assistant/device_tools.h"
#include "../assistant/assistant.h"
#include "version.h"
#include <WiFi.h>
#include <Preferences.h>

WebServerManager::WebServerManager()
    : server(nullptr)
    , settingsMenu(nullptr)
    , pomodoroTimer(nullptr)
    , wifiManager(nullptr)
    , otaManager(nullptr)
    , breathingExercise(nullptr)
    , countdownTimer(nullptr)
    , reminderManager(nullptr)
    , settingsChanged(false)
    , expressionCallback(nullptr)
    , audioTestCallback(nullptr)
    , moodGetterCallback(nullptr)
{
}

WebServerManager::~WebServerManager() {
    stop();
}

bool WebServerManager::begin(SettingsMenu* settings, PomodoroTimer* pomodoro, WiFiManager* wifi, OtaManager* ota) {
    settingsMenu = settings;
    pomodoroTimer = pomodoro;
    wifiManager = wifi;
    otaManager = ota;

    if (server != nullptr) {
        Serial.println("[WebServer] Already running");
        return true;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 30;  // 28 web handlers + headroom
    config.stack_size = 8192;  // Larger stack for OTA uploads

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

    httpd_uri_t timerStartUri = {
        .uri = "/api/timer/start",
        .method = HTTP_POST,
        .handler = handleTimerStart,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &timerStartUri);

    httpd_uri_t timerStopUri = {
        .uri = "/api/timer/stop",
        .method = HTTP_POST,
        .handler = handleTimerStop,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &timerStopUri);

    httpd_uri_t getRemindersUri = {
        .uri = "/api/reminders",
        .method = HTTP_GET,
        .handler = handleGetReminders,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &getRemindersUri);

    httpd_uri_t postReminderUri = {
        .uri = "/api/reminders",
        .method = HTTP_POST,
        .handler = handlePostReminder,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &postReminderUri);

    httpd_uri_t deleteReminderUri = {
        .uri = "/api/reminders/delete",
        .method = HTTP_POST,
        .handler = handleDeleteReminder,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &deleteReminderUri);

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

    // OTA endpoints
    httpd_uri_t systemInfoUri = {
        .uri = "/api/system/info",
        .method = HTTP_GET,
        .handler = handleGetSystemInfo,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &systemInfoUri);

    httpd_uri_t otaUploadUri = {
        .uri = "/api/ota/upload",
        .method = HTTP_POST,
        .handler = handleOtaUpload,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &otaUploadUri);

    httpd_uri_t otaStatusUri = {
        .uri = "/api/ota/status",
        .method = HTTP_GET,
        .handler = handleGetOtaStatus,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &otaStatusUri);

    httpd_uri_t otaCancelUri = {
        .uri = "/api/ota/cancel",
        .method = HTTP_POST,
        .handler = handleOtaCancel,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &otaCancelUri);

    httpd_uri_t systemRestartUri = {
        .uri = "/api/system/restart",
        .method = HTTP_POST,
        .handler = handleSystemRestart,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &systemRestartUri);

    httpd_uri_t systemRollbackUri = {
        .uri = "/api/system/rollback",
        .method = HTTP_POST,
        .handler = handleSystemRollback,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &systemRollbackUri);

    // Breathing/Wellness endpoints
    httpd_uri_t breathingStartUri = {
        .uri = "/api/breathing/start",
        .method = HTTP_POST,
        .handler = handleBreathingStart,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &breathingStartUri);

    // Assistant endpoints
    httpd_uri_t assistantStatusUri = {
        .uri = "/api/assistant/status",
        .method = HTTP_GET,
        .handler = handleAssistantStatus,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &assistantStatusUri);

    httpd_uri_t assistantClearUri = {
        .uri = "/api/assistant/clear",
        .method = HTTP_POST,
        .handler = handleAssistantClear,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &assistantClearUri);

    httpd_uri_t assistantSettingsGetUri = {
        .uri = "/api/assistant/settings",
        .method = HTTP_GET,
        .handler = handleGetAssistantSettings,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &assistantSettingsGetUri);

    httpd_uri_t assistantSettingsPostUri = {
        .uri = "/api/assistant/settings",
        .method = HTTP_POST,
        .handler = handlePostAssistantSettings,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &assistantSettingsPostUri);

    // MCP endpoints
    httpd_uri_t mcpServersGetUri = {
        .uri = "/api/mcp/servers",
        .method = HTTP_GET,
        .handler = handleGetMcpServers,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &mcpServersGetUri);

    httpd_uri_t mcpServersPostUri = {
        .uri = "/api/mcp/servers",
        .method = HTTP_POST,
        .handler = handlePostMcpServer,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &mcpServersPostUri);

    httpd_uri_t mcpDiscoverUri = {
        .uri = "/api/mcp/discover",
        .method = HTTP_POST,
        .handler = handleMcpDiscover,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &mcpDiscoverUri);

    // Initialize MCP SSE server on its own TCP port
    mcpServer.setToolExecutor([](const String& name, const String& args) -> String {
        return executeDeviceTool(name.c_str(), args.c_str());
    });
    registerMcpDeviceTools(mcpServer);
    mcpServer.begin();  // Starts dedicated TCP server on port 3001

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

    // Apply countdown timer settings
    if (self->countdownTimer) {
        if (doc["timerTickingEnabled"].is<bool>()) {
            self->countdownTimer->setTickingEnabled(doc["timerTickingEnabled"].as<bool>());
        }
    }

    // Apply breathing settings
    if (self->breathingExercise) {
        if (doc["breathingEnabled"].is<bool>()) {
            self->breathingExercise->setEnabled(doc["breathingEnabled"].as<bool>());
        }
        if (doc["breathingSoundEnabled"].is<bool>()) {
            self->breathingExercise->setSoundEnabled(doc["breathingSoundEnabled"].as<bool>());
        }
        if (doc["breathingStartHour"].is<int>() || doc["breathingEndHour"].is<int>()) {
            int start = doc["breathingStartHour"].is<int>()
                ? doc["breathingStartHour"].as<int>()
                : self->breathingExercise->getStartHour();
            int end = doc["breathingEndHour"].is<int>()
                ? doc["breathingEndHour"].as<int>()
                : self->breathingExercise->getEndHour();
            self->breathingExercise->setTimeWindow(start, end);
        }
        if (doc["breathingIntervalMinutes"].is<int>()) {
            self->breathingExercise->setIntervalMinutes(doc["breathingIntervalMinutes"].as<int>());
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

esp_err_t WebServerManager::handleTimerStart(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    // Read body for minutes parameter
    int minutes = 5;  // default
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret > 0) {
        buf[ret] = '\0';
        JsonDocument doc;
        if (!deserializeJson(doc, buf)) {
            minutes = doc["minutes"] | 5;
        }
    }

    if (self->countdownTimer && !self->countdownTimer->isActive()) {
        self->countdownTimer->start(minutes * 60, "TIMER");
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerManager::handleTimerStop(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    if (self->countdownTimer && self->countdownTimer->isActive()) {
        self->countdownTimer->stop();
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerManager::handleGetReminders(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    if (self->reminderManager) {
        for (const auto& r : self->reminderManager->getReminders()) {
            JsonObject obj = arr.add<JsonObject>();
            obj["hour"] = r.hour;
            obj["minute"] = r.minute;
            obj["message"] = r.message;
            obj["recurring"] = r.recurring;
        }
    }

    String output;
    serializeJson(doc, output);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, output.c_str());
    return ESP_OK;
}

esp_err_t WebServerManager::handlePostReminder(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    int hour = doc["hour"] | 0;
    int minute = doc["minute"] | 0;
    const char* message = doc["message"] | "";
    bool recurring = doc["recurring"] | false;

    bool ok = false;
    if (self->reminderManager) {
        ok = self->reminderManager->add(hour, minute, message, recurring);
    }

    httpd_resp_set_type(req, "application/json");
    if (ok) {
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else {
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Failed to add reminder\"}");
    }
    return ESP_OK;
}

esp_err_t WebServerManager::handleDeleteReminder(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, buf)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    int index = doc["index"] | -1;
    if (self->reminderManager && index >= 0) {
        self->reminderManager->remove(index);
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
// OTA Handlers
// ============================================================================

esp_err_t WebServerManager::handleGetSystemInfo(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    JsonDocument doc;
    doc["version"] = OtaManager::getVersion();
    doc["buildDate"] = OtaManager::getBuildDate();
    doc["releaseNotes"] = FIRMWARE_RELEASE_NOTES;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["minFreeHeap"] = ESP.getMinFreeHeap();
    doc["uptimeSeconds"] = millis() / 1000;

    if (self->otaManager) {
        doc["partitionLabel"] = self->otaManager->getPartitionLabel();
        doc["otaPartitionSize"] = self->otaManager->getOtaPartitionSize();
        doc["canRollback"] = self->otaManager->canRollback();
        doc["signatureRequired"] = self->otaManager->hasSigningKey();
    }

    String json;
    serializeJson(doc, json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

esp_err_t WebServerManager::handleOtaUpload(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    if (!self->otaManager) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA not initialized");
        return ESP_FAIL;
    }

    size_t totalSize = req->content_len;
    if (totalSize == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No content");
        return ESP_FAIL;
    }

    Serial.printf("[WebServer] OTA upload starting, size: %u bytes\n", totalSize);

    // Start OTA upload
    if (!self->otaManager->startUpload(totalSize)) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                           self->otaManager->getErrorMessage());
        return ESP_FAIL;
    }

    // Read and write in chunks
    const size_t CHUNK_SIZE = 4096;
    uint8_t* buffer = (uint8_t*)malloc(CHUNK_SIZE);
    if (!buffer) {
        self->otaManager->cancelUpload();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    size_t remaining = totalSize;
    bool success = true;

    while (remaining > 0) {
        size_t toRead = (remaining > CHUNK_SIZE) ? CHUNK_SIZE : remaining;
        int received = httpd_req_recv(req, (char*)buffer, toRead);

        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;  // Retry on timeout
            }
            Serial.println("[WebServer] OTA receive error");
            success = false;
            break;
        }

        if (!self->otaManager->writeChunk(buffer, received)) {
            success = false;
            break;
        }

        remaining -= received;
    }

    free(buffer);

    if (!success) {
        self->otaManager->cancelUpload();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                           self->otaManager->getErrorMessage());
        return ESP_FAIL;
    }

    // Finalize upload
    if (!self->otaManager->finishUpload()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                           self->otaManager->getErrorMessage());
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Update complete. Restarting...\"}");

    // Schedule restart
    delay(500);
    self->otaManager->restart();

    return ESP_OK;
}

esp_err_t WebServerManager::handleGetOtaStatus(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    JsonDocument doc;
    if (self->otaManager) {
        doc["state"] = self->otaManager->getStateString();
        doc["progress"] = self->otaManager->getProgress();
        doc["bytesReceived"] = self->otaManager->getBytesReceived();
        doc["totalBytes"] = self->otaManager->getTotalBytes();
        const char* errMsg = self->otaManager->getErrorMessage();
        if (errMsg && errMsg[0] != '\0') {
            doc["errorMessage"] = errMsg;
        } else {
            doc["errorMessage"] = nullptr;
        }
    } else {
        doc["state"] = "unavailable";
        doc["errorMessage"] = "OTA not initialized";
    }

    String json;
    serializeJson(doc, json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

esp_err_t WebServerManager::handleOtaCancel(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    if (self->otaManager) {
        self->otaManager->cancelUpload();
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerManager::handleSystemRestart(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Restarting...\"}");

    delay(500);
    if (self->otaManager) {
        self->otaManager->restart();
    } else {
        ESP.restart();
    }

    return ESP_OK;
}

esp_err_t WebServerManager::handleSystemRollback(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    if (!self->otaManager) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA not initialized");
        return ESP_FAIL;
    }

    if (!self->otaManager->canRollback()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No previous firmware to rollback to");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Rolling back...\"}");

    delay(500);
    self->otaManager->rollback();

    return ESP_OK;
}

// ============================================================================
// Breathing/Wellness Handlers
// ============================================================================

esp_err_t WebServerManager::handleBreathingStart(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    if (!self->breathingExercise) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Breathing not initialized");
        return ESP_FAIL;
    }

    self->breathingExercise->triggerNow();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

// ============================================================================
// Assistant Handlers
// ============================================================================

esp_err_t WebServerManager::handleAssistantStatus(httpd_req_t* req) {
    JsonDocument doc;

    // Get assistant state if available
    extern class Assistant assistant;
    doc["state"] = "Idle"; // Default
    doc["contextTokens"] = 0;

    String response;
    serializeJson(doc, response);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response.c_str());
    return ESP_OK;
}

esp_err_t WebServerManager::handleAssistantClear(httpd_req_t* req) {
    // Clear assistant history
    extern class Assistant assistant;

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

esp_err_t WebServerManager::handleGetAssistantSettings(httpd_req_t* req) {
    Preferences prefs;
    prefs.begin("assistant", true);

    JsonDocument doc;
    doc["llmProvider"] = prefs.getString("llmProv", "claude");
    doc["llmApiKey"] = prefs.getString("llmKey", "").length() > 0 ? "********" : "";
    doc["openaiVoiceKey"] = prefs.getString("voiceKey", "").length() > 0 ? "********" : "";
    doc["ttsVoice"] = prefs.getString("ttsVoice", "alloy");
    doc["ttsSpeed"] = prefs.getFloat("ttsSpeed", 1.0);
    doc["wakeWordEnabled"] = prefs.getBool("wakeWord", true);
    doc["pttEnabled"] = prefs.getBool("ptt", true);
    doc["wakeSensitivity"] = prefs.getInt("wakeSens", 50);
    doc["systemPrompt"] = prefs.getString("sysPrompt", "You are DeskBuddy, a helpful desk companion.");

    prefs.end();

    String response;
    serializeJson(doc, response);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response.c_str());
    return ESP_OK;
}

esp_err_t WebServerManager::handlePostAssistantSettings(httpd_req_t* req) {
    char buf[2048];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, buf);
    if (error) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    Preferences prefs;
    prefs.begin("assistant", false);

    if (doc["llmProvider"].is<const char*>()) {
        prefs.putString("llmProv", doc["llmProvider"].as<String>());
    }
    if (doc["llmApiKey"].is<const char*>() && strlen(doc["llmApiKey"]) > 0) {
        prefs.putString("llmKey", doc["llmApiKey"].as<String>());
    }
    if (doc["openaiVoiceKey"].is<const char*>() && strlen(doc["openaiVoiceKey"]) > 0) {
        prefs.putString("voiceKey", doc["openaiVoiceKey"].as<String>());
    }
    if (doc["ttsVoice"].is<const char*>()) {
        prefs.putString("ttsVoice", doc["ttsVoice"].as<String>());
    }
    if (doc["ttsSpeed"].is<float>()) {
        prefs.putFloat("ttsSpeed", doc["ttsSpeed"].as<float>());
    }
    if (doc["wakeWordEnabled"].is<bool>()) {
        prefs.putBool("wakeWord", doc["wakeWordEnabled"].as<bool>());
    }
    if (doc["pttEnabled"].is<bool>()) {
        prefs.putBool("ptt", doc["pttEnabled"].as<bool>());
    }
    if (doc["wakeSensitivity"].is<int>()) {
        prefs.putInt("wakeSens", doc["wakeSensitivity"].as<int>());
    }
    if (doc["systemPrompt"].is<const char*>()) {
        prefs.putString("sysPrompt", doc["systemPrompt"].as<String>());
    }

    prefs.end();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

// ============================================================================
// MCP Handlers
// ============================================================================

esp_err_t WebServerManager::handleGetMcpServers(httpd_req_t* req) {
    extern class MCPClient mcpClient;

    JsonDocument doc;
    JsonArray servers = doc["servers"].to<JsonArray>();

    int count = mcpClient.getServerCount();
    for (int i = 0; i < count; i++) {
        const MCPServerConfig* cfg = mcpClient.getServer(i);
        if (cfg) {
            JsonObject s = servers.add<JsonObject>();
            s["name"] = cfg->name;
            s["url"] = cfg->url;
            s["enabled"] = cfg->enabled;
            s["connected"] = cfg->connected;
            if (cfg->lastError.length() > 0) {
                s["error"] = cfg->lastError;
            }
        }
    }

    String response;
    serializeJson(doc, response);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response.c_str());
    return ESP_OK;
}

esp_err_t WebServerManager::handlePostMcpServer(httpd_req_t* req) {
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, buf);
    if (error) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const char* name = doc["name"];
    const char* url = doc["url"];
    const char* apiKey = doc["apiKey"];

    if (!name || !url) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Name and URL required");
        return ESP_FAIL;
    }

    extern class MCPClient mcpClient;
    int index = mcpClient.addServer(name, url, apiKey);

    if (index >= 0) {
        mcpClient.saveConfig();
    }

    JsonDocument respDoc;
    respDoc["success"] = (index >= 0);
    respDoc["index"] = index;

    String response;
    serializeJson(respDoc, response);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response.c_str());
    return ESP_OK;
}

esp_err_t WebServerManager::handleMcpDiscover(httpd_req_t* req) {
    extern class MCPClient mcpClient;

    int toolCount = mcpClient.discoverTools();

    JsonDocument doc;
    doc["success"] = true;
    doc["toolCount"] = toolCount;

    String response;
    serializeJson(doc, response);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response.c_str());
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

    if (countdownTimer) {
        JsonObject timer = doc["timer"].to<JsonObject>();
        timer["tickingEnabled"] = countdownTimer->isTickingEnabled();
    }

    if (breathingExercise) {
        JsonObject breathing = doc["breathing"].to<JsonObject>();
        breathing["enabled"] = breathingExercise->isEnabled();
        breathing["soundEnabled"] = breathingExercise->isSoundEnabled();
        breathing["startHour"] = breathingExercise->getStartHour();
        breathing["endHour"] = breathingExercise->getEndHour();
        breathing["intervalMinutes"] = breathingExercise->getIntervalMinutes();
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

    // Countdown timer status
    if (countdownTimer) {
        JsonObject timer = doc["timer"].to<JsonObject>();
        timer["active"] = countdownTimer->isActive();
        timer["remainingSeconds"] = countdownTimer->getRemainingSeconds();
        timer["name"] = countdownTimer->getTimerName();
    }

    // Breathing status
    if (breathingExercise) {
        JsonObject breathing = doc["breathing"].to<JsonObject>();
        breathing["enabled"] = breathingExercise->isEnabled();
        breathing["soundEnabled"] = breathingExercise->isSoundEnabled();
        breathing["active"] = breathingExercise->isActive();
        breathing["startHour"] = breathingExercise->getStartHour();
        breathing["endHour"] = breathingExercise->getEndHour();
        breathing["intervalMinutes"] = breathingExercise->getIntervalMinutes();
    }

    // Reminders status
    if (reminderManager) {
        JsonArray reminders = doc["reminders"].to<JsonArray>();
        for (const auto& r : reminderManager->getReminders()) {
            JsonObject obj = reminders.add<JsonObject>();
            obj["hour"] = r.hour;
            obj["minute"] = r.minute;
            obj["message"] = r.message;
            obj["recurring"] = r.recurring;
        }
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

        /* Input group (input + button side by side) */
        .input-group {
            display: flex;
            gap: 8px;
            align-items: stretch;
        }
        .input-group .btn {
            width: auto;
            flex-shrink: 0;
        }
        .input-group .wifi-input {
            flex: 1;
            margin-bottom: 0;
        }

        /* OTA upload */
        .ota-dropzone {
            border: 2px dashed var(--border);
            border-radius: 8px;
            padding: 40px;
            text-align: center;
            cursor: pointer;
            transition: all 0.2s;
        }
        .ota-dropzone:hover, .ota-dropzone.dragover {
            border-color: var(--primary);
            background: rgba(223, 255, 0, 0.05);
        }
        .ota-icon {
            font-size: 48px;
            margin-bottom: 12px;
            color: var(--muted-foreground);
        }
        .ota-progress {
            margin-top: 16px;
        }
        .ota-progress-bar {
            height: 8px;
            background: var(--secondary);
            border-radius: 4px;
            overflow: hidden;
        }
        .ota-progress-fill {
            height: 100%;
            background: var(--primary);
            width: 0%;
            transition: width 0.3s;
        }
        .ota-status {
            margin-top: 8px;
            font-size: 14px;
            color: var(--muted-foreground);
        }
        .ota-success { color: #00FF00; }
        .ota-error { color: var(--destructive); }

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

        /* Modal */
        .modal {
            position: fixed;
            inset: 0;
            background: rgba(0, 0, 0, 0.8);
            display: flex;
            align-items: center;
            justify-content: center;
            z-index: 300;
        }
        .modal-content {
            background: var(--card);
            border: 1px solid var(--border);
            border-radius: 12px;
            width: 90%;
            max-width: 400px;
            overflow: hidden;
        }
        .modal-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 16px 20px;
            border-bottom: 1px solid var(--border);
        }
        .modal-header span {
            font-weight: 600;
        }
        .modal-close {
            background: none;
            border: none;
            color: var(--muted-foreground);
            font-size: 24px;
            cursor: pointer;
        }
        .modal-close:hover { color: var(--foreground); }
        .modal-body {
            padding: 20px;
        }
        .modal-footer {
            display: flex;
            justify-content: flex-end;
            gap: 12px;
            padding: 16px 20px;
            border-top: 1px solid var(--border);
        }

        /* Accordions */
        .accordion {
            border: 1px solid var(--border);
            border-radius: 8px;
            margin-bottom: 16px;
            overflow: hidden;
        }
        .accordion-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 16px 20px;
            background: var(--card);
            cursor: pointer;
            user-select: none;
            transition: background 0.2s;
        }
        .accordion-header:hover {
            background: var(--secondary);
        }
        .accordion-title {
            font-weight: 500;
        }
        .accordion-icon {
            transition: transform 0.2s;
            color: var(--muted-foreground);
        }
        .accordion.open .accordion-icon {
            transform: rotate(180deg);
        }
        .accordion-content {
            display: none;
            padding: 20px;
            background: var(--card);
            border-top: 1px solid var(--border);
        }
        .accordion.open .accordion-content {
            display: block;
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
        <button class="tab" data-tab="productivity">Productivity</button>
        <button class="tab" data-tab="mindfulness">Mindfulness</button>
        <button class="tab" data-tab="assistant">Assistant</button>
        <button class="tab" data-tab="settings">Settings</button>
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

        <!-- Settings (accordions) -->
        <section id="settings" class="section">
            <span class="section-header">Device Settings</span>

            <!-- Display Accordion (open by default) -->
            <div class="accordion open" data-accordion="display">
                <div class="accordion-header" onclick="toggleAccordion('display')">
                    <span class="accordion-title">Display</span>
                    <span class="accordion-icon">&#9660;</span>
                </div>
                <div class="accordion-content">
                    <div class="form-group">
                        <div class="form-label">
                            <span>Brightness</span>
                            <span class="form-value" id="brightness-val">100%</span>
                        </div>
                        <input type="range" id="brightness" min="0" max="100" value="100">
                    </div>
                    <div style="margin-top: 20px;">
                        <div class="card-title">Eye Color</div>
                        <div class="color-grid" id="color-grid"></div>
                    </div>
                </div>
            </div>

            <!-- Audio Accordion -->
            <div class="accordion" data-accordion="audio">
                <div class="accordion-header" onclick="toggleAccordion('audio')">
                    <span class="accordion-title">Audio</span>
                    <span class="accordion-icon">&#9660;</span>
                </div>
                <div class="accordion-content">
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
            </div>

            <!-- Time Accordion -->
            <div class="accordion" data-accordion="time">
                <div class="accordion-header" onclick="toggleAccordion('time')">
                    <span class="accordion-title">Time</span>
                    <span class="accordion-icon">&#9660;</span>
                </div>
                <div class="accordion-content">
                    <div class="status-row">
                        <span class="status-row-label">NTP Sync</span>
                        <span class="status-row-value" id="ntp-status">--</span>
                    </div>
                    <div class="form-group" style="margin-top: 16px;">
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
                    <div class="toggle-row" style="margin-top: 16px;">
                        <span class="toggle-label">24-hour format</span>
                        <label class="toggle">
                            <input type="checkbox" id="time-24h">
                            <span class="slider"></span>
                        </label>
                    </div>
                </div>
            </div>

            <!-- WiFi Accordion -->
            <div class="accordion" data-accordion="wifi">
                <div class="accordion-header" onclick="toggleAccordion('wifi')">
                    <span class="accordion-title">WiFi</span>
                    <span class="accordion-icon">&#9660;</span>
                </div>
                <div class="accordion-content">
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
                    <div style="margin-top: 20px;">
                        <div class="card-title">Available Networks</div>
                        <div class="wifi-list" id="wifi-list"></div>
                        <button class="btn btn-secondary" onclick="scanWiFi()">Scan Networks</button>
                        <div id="wifi-connect-form" class="hidden" style="margin-top: 16px;">
                            <input type="text" id="wifi-ssid-input" class="wifi-input" placeholder="Network name">
                            <input type="password" id="wifi-pass-input" class="wifi-input" placeholder="Password">
                            <button class="btn btn-primary" onclick="connectWiFi()">Connect</button>
                        </div>
                    </div>
                    <div style="margin-top: 20px;">
                        <div class="card-title">Danger Zone</div>
                        <div style="display: flex; gap: 8px;">
                            <button class="btn btn-danger" style="flex: 1 1 0; min-width: 0;" onclick="forgetWiFi()">Forget Network</button>
                            <button class="btn btn-danger" style="flex: 1 1 0; min-width: 0; margin-top: 0;" onclick="disableWiFi()">Disable WiFi</button>
                        </div>
                        <p style="margin-top: 12px; font-size: 12px; color: #888;">Disabling WiFi will disconnect this page.</p>
                    </div>
                </div>
            </div>

            <!-- Assistant Settings Accordion -->
            <div class="accordion" data-accordion="assistant-settings">
                <div class="accordion-header" onclick="toggleAccordion('assistant-settings')">
                    <span class="accordion-title">Assistant</span>
                    <span class="accordion-icon">&#9660;</span>
                </div>
                <div class="accordion-content">
                    <div class="card-title">LLM Configuration</div>
                    <div class="form-group">
                        <div class="form-label"><span>LLM Provider</span></div>
                        <select id="llm-provider" class="wifi-input" onchange="updateLlmKeyPlaceholder()">
                            <option value="claude">Claude (Anthropic)</option>
                            <option value="openai">OpenAI (GPT-4)</option>
                        </select>
                    </div>
                    <div class="form-group">
                        <div class="form-label"><span id="llm-key-label">Claude API Key</span></div>
                        <div class="input-group">
                            <input type="password" id="llm-api-key" class="wifi-input" placeholder="sk-ant-...">
                            <button class="btn btn-secondary" onclick="testLlmApi()">Test</button>
                        </div>
                    </div>

                    <div class="card-title" style="margin-top: 24px;">Voice Configuration (OpenAI)</div>
                    <p style="color: var(--muted-foreground); font-size: 12px; margin-bottom: 12px;">
                        Uses OpenAI Whisper for speech-to-text and OpenAI TTS for voice output.
                    </p>
                    <div class="form-group">
                        <div class="form-label"><span>OpenAI API Key</span></div>
                        <div class="input-group">
                            <input type="password" id="openai-voice-key" class="wifi-input" placeholder="sk-...">
                            <button class="btn btn-secondary" onclick="testVoiceApi()">Test</button>
                        </div>
                        <p style="color: var(--muted-foreground); font-size: 11px; margin-top: 4px;">
                            Same key as LLM if using OpenAI for both.
                        </p>
                    </div>

                    <div class="card-title" style="margin-top: 24px;">Voice Settings</div>
                    <div class="form-group">
                        <div class="form-label"><span>Voice</span></div>
                        <div class="input-group">
                            <select id="tts-voice" class="wifi-input">
                                <option value="alloy">Alloy</option>
                                <option value="echo">Echo</option>
                                <option value="fable">Fable</option>
                                <option value="onyx">Onyx</option>
                                <option value="nova">Nova</option>
                                <option value="shimmer">Shimmer</option>
                            </select>
                            <button class="btn btn-secondary" onclick="previewVoice()">Preview</button>
                        </div>
                    </div>
                    <div class="form-group">
                        <div class="form-label">
                            <span>Speech Speed</span>
                            <span class="form-value" id="tts-speed-val">1.0x</span>
                        </div>
                        <input type="range" id="tts-speed" min="0.5" max="2.0" step="0.1" value="1.0">
                    </div>

                    <div class="card-title" style="margin-top: 24px;">Activation</div>
                    <div class="toggle-row">
                        <span class="toggle-label">Wake Word ("Hey Buddy")</span>
                        <label class="toggle">
                            <input type="checkbox" id="wake-word-enabled">
                            <span class="slider"></span>
                        </label>
                    </div>
                    <p style="color: var(--muted-foreground); font-size: 11px; margin: 4px 0 12px 0;">
                        Requires ESP-SR. Currently in stub mode - use push-to-talk.
                    </p>
                    <div class="toggle-row">
                        <span class="toggle-label">Push-to-Talk (hold screen)</span>
                        <label class="toggle">
                            <input type="checkbox" id="ptt-enabled" checked>
                            <span class="slider"></span>
                        </label>
                    </div>
                    <p style="color: var(--muted-foreground); font-size: 11px; margin: 4px 0 12px 0;">
                        When enabled, holding the screen activates voice input. Disables petting gesture.
                    </p>
                    <div class="form-group" style="margin-top: 16px;">
                        <div class="form-label">
                            <span>Wake Word Sensitivity</span>
                            <span class="form-value" id="wake-sensitivity-val">50%</span>
                        </div>
                        <input type="range" id="wake-sensitivity" min="0" max="100" value="50">
                    </div>

                    <div class="card-title" style="margin-top: 24px;">System Prompt</div>
                    <div class="form-group">
                        <textarea id="system-prompt" class="wifi-input" rows="4" style="resize: vertical; min-height: 100px;" placeholder="You are DeskBuddy, a helpful desk companion..."></textarea>
                    </div>
                    <button class="btn btn-primary" onclick="saveAssistantSettings()">Save Settings</button>
                </div>
            </div>

            <!-- System Accordion -->
            <div class="accordion" data-accordion="system">
                <div class="accordion-header" onclick="toggleAccordion('system')">
                    <span class="accordion-title">System</span>
                    <span class="accordion-icon">&#9660;</span>
                </div>
                <div class="accordion-content">
                    <div class="dashboard-grid" style="margin-bottom: 20px;">
                        <div class="stat-card">
                            <div class="stat-label">Version</div>
                            <div class="stat-value" id="sys-version">--</div>
                        </div>
                        <div class="stat-card">
                            <div class="stat-label">Built</div>
                            <div class="stat-value" id="sys-build">--</div>
                        </div>
                        <div class="stat-card">
                            <div class="stat-label">Partition</div>
                            <div class="stat-value" id="sys-partition">--</div>
                        </div>
                        <div class="stat-card">
                            <div class="stat-label">Free Heap</div>
                            <div class="stat-value" id="sys-heap">--</div>
                        </div>
                    </div>
                    <div class="card-title">What's New</div>
                    <pre id="sys-notes" style="margin: 0 0 20px 0; font-family: inherit; white-space: pre-wrap; color: var(--muted-foreground); font-size: 14px;">Loading...</pre>
                    <div class="card-title">Update Firmware</div>
                    <div id="ota-dropzone" class="ota-dropzone" style="margin-bottom: 20px;">
                        <div class="ota-icon">&#8681;</div>
                        <div>Drag firmware.bin here</div>
                        <div style="font-size: 12px; color: var(--muted-foreground);">or click to browse</div>
                        <input type="file" id="ota-file" accept=".bin" style="display: none;">
                    </div>
                    <div id="ota-progress" class="ota-progress hidden">
                        <div class="ota-progress-bar">
                            <div class="ota-progress-fill" id="ota-fill"></div>
                        </div>
                        <div class="ota-status" id="ota-status">Uploading...</div>
                    </div>
                    <div class="card-title" style="margin-top: 20px;">Danger Zone</div>
                    <div style="display: flex; gap: 8px;">
                        <button class="btn btn-danger" style="flex: 1 1 0; min-width: 0;" onclick="restartDevice()">Restart</button>
                        <button class="btn btn-danger" style="flex: 1 1 0; min-width: 0; margin-top: 0;" id="btn-rollback" onclick="rollbackFirmware()">Rollback</button>
                    </div>
                    <p style="margin-top: 12px; font-size: 12px; color: #888;">Rollback reverts to the previous firmware version.</p>
                </div>
            </div>
        </section>

        <!-- Productivity -->
        <section id="productivity" class="section">
            <span class="section-header">Productivity</span>

            <!-- Reminders Accordion -->
            <div class="accordion open" data-accordion="reminders">
                <div class="accordion-header" onclick="toggleAccordion('reminders')">
                    <span class="accordion-title">Reminders</span>
                    <span class="accordion-icon">&#9660;</span>
                </div>
                <div class="accordion-content">
                    <div id="reminder-list"></div>
                    <div class="card">
                        <div class="card-title">Add Reminder</div>
                        <div class="form-group">
                            <div class="form-label"><span>Time</span></div>
                            <input type="time" id="reminder-time" value="12:00" style="background:#1A1A1A;color:#F2F2F2;border:1px solid #333;border-radius:8px;padding:8px 12px;font-family:'JetBrains Mono',monospace;font-size:14px;width:100%;box-sizing:border-box;">
                        </div>
                        <div class="form-group">
                            <div class="form-label">
                                <span>Message</span>
                                <span class="form-value" id="reminder-char-count">0/48</span>
                            </div>
                            <input type="text" id="reminder-message" maxlength="48" placeholder="Take out trash" style="background:#1A1A1A;color:#F2F2F2;border:1px solid #333;border-radius:8px;padding:8px 12px;font-family:'JetBrains Mono',monospace;font-size:14px;width:100%;box-sizing:border-box;">
                        </div>
                        <div class="toggle-row">
                            <span class="toggle-label">Repeat Daily</span>
                            <label class="toggle">
                                <input type="checkbox" id="reminder-recurring">
                                <span class="slider"></span>
                            </label>
                        </div>
                        <button class="btn btn-primary" onclick="addReminder()" style="margin-top:12px;">Add Reminder</button>
                    </div>
                </div>
            </div>

            <!-- Countdown Timer Accordion -->
            <div class="accordion open" data-accordion="countdown">
                <div class="accordion-header" onclick="toggleAccordion('countdown')">
                    <span class="accordion-title">Countdown Timer</span>
                    <span class="accordion-icon">&#9660;</span>
                </div>
                <div class="accordion-content">
                    <div class="card">
                        <div class="pomodoro-display">
                            <div class="pomodoro-time" id="timer-time">--:--</div>
                            <div class="pomodoro-state" id="timer-state">Ready</div>
                        </div>
                        <button class="btn btn-primary" id="btn-timer-start" onclick="startCountdown()">Start</button>
                        <button class="btn btn-danger hidden" id="btn-timer-stop" onclick="stopCountdown()">Stop</button>
                    </div>

                    <div class="card">
                        <div class="card-title">Duration</div>
                        <div class="form-group">
                            <div class="form-label">
                                <span>Minutes</span>
                                <span class="form-value" id="timerMinutes-val">5 min</span>
                            </div>
                            <input type="range" id="timerMinutes" min="1" max="60" value="5">
                        </div>
                    </div>

                    <div class="card">
                        <div class="card-title">Options</div>
                        <div class="toggle-row">
                            <span class="toggle-label">Ticking Sound (last 60s)</span>
                            <label class="toggle">
                                <input type="checkbox" id="timerTickingEnabled" checked>
                                <span class="slider"></span>
                            </label>
                        </div>
                    </div>
                </div>
            </div>

            <!-- Pomodoro Accordion -->
            <div class="accordion open" data-accordion="pomodoro">
                <div class="accordion-header" onclick="toggleAccordion('pomodoro')">
                    <span class="accordion-title">Pomodoro Timer</span>
                    <span class="accordion-icon">&#9660;</span>
                </div>
                <div class="accordion-content">
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

        <!-- Mindfulness -->
        <section id="mindfulness" class="section">
            <span class="section-header">Mindfulness</span>

            <!-- Box Breathing Accordion -->
            <div class="accordion open" data-accordion="box-breathing">
                <div class="accordion-header" onclick="toggleAccordion('box-breathing')">
                    <span class="accordion-title">Box Breathing</span>
                    <span class="accordion-icon">&#9660;</span>
                </div>
                <div class="accordion-content">
                    <div class="card">
                        <p style="color: var(--muted-foreground); margin-bottom: 16px;">
                            Inhale 5s, hold 5s, exhale 5s, hold 5s &times; 3 cycles (60 seconds total)
                        </p>
                        <button class="btn btn-primary" onclick="startBreathing()">Breathe Now</button>
                    </div>

                    <div class="card">
                        <div class="toggle-row">
                            <span class="toggle-label">Enable Scheduled Reminders</span>
                            <label class="toggle">
                                <input type="checkbox" id="breathing-enabled">
                                <span class="slider"></span>
                            </label>
                        </div>
                        <div class="toggle-row" style="margin-top: 12px;">
                            <span class="toggle-label">Sound</span>
                            <label class="toggle">
                                <input type="checkbox" id="breathing-sound-enabled" checked>
                                <span class="slider"></span>
                            </label>
                        </div>
                    </div>

                    <div class="card" id="breathing-schedule-card">
                        <div class="card-title">Schedule</div>

                        <div class="form-group">
                            <div class="form-label"><span>Active Hours</span></div>
                            <div style="display: flex; align-items: center; gap: 12px; margin-top: 8px;">
                                <select id="breathing-start-hour" class="select-input"></select>
                                <span>to</span>
                                <select id="breathing-end-hour" class="select-input"></select>
                            </div>
                            <p style="color: var(--muted-foreground); font-size: 12px; margin-top: 8px;">Reminders only appear during this window</p>
                        </div>

                        <div class="form-group" style="margin-top: 16px;">
                            <div class="form-label">
                                <span>Remind Every</span>
                                <span class="form-value" id="breathing-interval-val">60 min</span>
                            </div>
                            <input type="range" id="breathing-interval" min="30" max="180" step="15" value="60">
                        </div>
                    </div>
                </div>
            </div>
        </section>

        <!-- Assistant -->
        <section id="assistant" class="section">
            <span class="section-header">Voice Assistant</span>

            <!-- Assistant Status Card -->
            <div class="card">
                <div class="card-title">Status</div>
                <div class="dashboard-grid" style="margin-top: 12px;">
                    <div class="stat-card">
                        <div class="stat-label">State</div>
                        <div class="stat-value" id="assistant-state">Idle</div>
                    </div>
                    <div class="stat-card">
                        <div class="stat-label">Context Tokens</div>
                        <div class="stat-value" id="assistant-tokens">0 / 8000</div>
                    </div>
                </div>
                <button class="btn btn-secondary" onclick="clearAssistantHistory()" style="margin-top: 16px;">Clear History</button>
            </div>

            <!-- MCP Server Status Accordion -->
            <div class="accordion open" data-accordion="mcp-server">
                <div class="accordion-header" onclick="toggleAccordion('mcp-server')">
                    <span class="accordion-title">DeskBuddy MCP Server</span>
                    <span class="accordion-icon">&#9660;</span>
                </div>
                <div class="accordion-content">
                    <div class="status-row">
                        <span class="status-row-label">Endpoint</span>
                        <span class="status-row-value" id="mcp-endpoint">--</span>
                    </div>
                    <div class="status-row">
                        <span class="status-row-label">Status</span>
                        <span class="status-row-value" id="mcp-server-status">Running</span>
                    </div>
                    <div class="status-row">
                        <span class="status-row-label">Exposed Tools</span>
                        <span class="status-row-value" id="mcp-tools-count">6</span>
                    </div>
                    <div style="margin-top: 16px;">
                        <div class="form-label"><span>Claude Desktop Config</span></div>
                        <div style="position: relative;">
                            <pre id="mcp-claude-config" style="background: var(--card-bg); border: 1px solid var(--border); border-radius: 8px; padding: 12px; font-size: 12px; overflow-x: auto; white-space: pre; margin: 0; color: var(--foreground); font-family: monospace;"></pre>
                            <button onclick="copyMcpConfig()" style="position: absolute; top: 8px; right: 8px; background: var(--border); border: none; border-radius: 4px; padding: 4px 10px; cursor: pointer; color: var(--foreground); font-size: 12px;" id="mcp-copy-btn">Copy</button>
                        </div>
                    </div>
                </div>
            </div>

            <!-- MCP Client Connections Accordion -->
            <div class="accordion" data-accordion="mcp-clients">
                <div class="accordion-header" onclick="toggleAccordion('mcp-clients')">
                    <span class="accordion-title">Connected MCP Servers</span>
                    <span class="accordion-icon">&#9660;</span>
                </div>
                <div class="accordion-content">
                    <p style="color: var(--muted-foreground); margin-bottom: 16px;">
                        Connect to external MCP servers to give DeskBuddy access to more tools.
                    </p>
                    <div id="mcp-servers-list">
                        <p style="color: var(--muted-foreground); font-style: italic;">No MCP servers configured</p>
                    </div>
                    <div style="margin-top: 16px;">
                        <button class="btn btn-primary" onclick="showAddMcpServer()">Add Server</button>
                        <button class="btn btn-secondary" onclick="discoverMcpTools()" style="margin-left: 8px;">Refresh Tools</button>
                    </div>
                </div>
            </div>
        </section>
    </main>

    <!-- MCP Server Add/Edit Modal -->
    <div id="mcp-modal" class="modal" style="display: none;">
        <div class="modal-content">
            <div class="modal-header">
                <span id="mcp-modal-title">Add MCP Server</span>
                <button class="modal-close" onclick="closeMcpModal()">&times;</button>
            </div>
            <div class="modal-body">
                <div class="form-group">
                    <div class="form-label"><span>Server Name</span></div>
                    <input type="text" id="mcp-name" class="wifi-input" placeholder="Home Assistant">
                </div>
                <div class="form-group">
                    <div class="form-label"><span>Server URL</span></div>
                    <input type="text" id="mcp-url" class="wifi-input" placeholder="http://192.168.1.100:8080">
                </div>
                <div class="form-group">
                    <div class="form-label"><span>API Key (optional)</span></div>
                    <input type="password" id="mcp-apikey" class="wifi-input" placeholder="Optional authentication key">
                </div>
                <input type="hidden" id="mcp-edit-index" value="-1">
            </div>
            <div class="modal-footer">
                <button class="btn btn-secondary" onclick="closeMcpModal()">Cancel</button>
                <button class="btn btn-primary" onclick="saveMcpServer()">Save</button>
            </div>
        </div>
    </div>

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
                // Load system info when Settings tab is opened
                if (tab.dataset.tab === 'settings') loadSystemInfo();
            });
        });

        // Accordion toggle
        function toggleAccordion(name) {
            const acc = document.querySelector('[data-accordion="' + name + '"]');
            if (acc) acc.classList.toggle('open');
        }

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

                // Countdown timer
                if (status.timer) {
                    updateTimerUI(status.timer);
                }

                // Reminders
                updateRemindersUI(status.reminders);

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

                if (settings.timer) {
                    document.getElementById('timerTickingEnabled').checked = settings.timer.tickingEnabled;
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

                // Breathing/Wellness settings
                if (settings.breathing) {
                    document.getElementById('breathing-enabled').checked = settings.breathing.enabled;
                    document.getElementById('breathing-sound-enabled').checked = settings.breathing.soundEnabled !== false;
                    document.getElementById('breathing-start-hour').value = settings.breathing.startHour;
                    document.getElementById('breathing-end-hour').value = settings.breathing.endHour;
                    document.getElementById('breathing-interval').value = settings.breathing.intervalMinutes;
                    document.getElementById('breathing-interval-val').textContent = settings.breathing.intervalMinutes + ' min';
                    document.getElementById('breathing-schedule-card').style.opacity = settings.breathing.enabled ? '1' : '0.5';
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

        // Reminders
        function updateRemindersUI(reminders) {
            const list = document.getElementById('reminder-list');
            if (!list) return;
            if (!reminders || reminders.length === 0) {
                list.innerHTML = '<div class="card" style="text-align:center;opacity:0.5;">No reminders set</div>';
                return;
            }
            let html = '';
            reminders.forEach((r, i) => {
                const time = r.hour.toString().padStart(2,'0') + ':' + r.minute.toString().padStart(2,'0');
                const badge = r.recurring ? ' <span style="color:#DFFF00;font-size:11px;">DAILY</span>' : '';
                html += '<div class="card" style="display:flex;justify-content:space-between;align-items:center;padding:10px 14px;">';
                html += '<div><span style="font-family:JetBrains Mono,monospace;color:#DFFF00;margin-right:10px;">' + time + '</span>';
                html += '<span>' + r.message + '</span>' + badge + '</div>';
                html += '<button style="background:#FF4444;color:#fff;border:none;border-radius:6px;width:28px;height:28px;min-width:28px;font-size:13px;cursor:pointer;flex-shrink:0;" onclick="deleteReminder(' + i + ')">&times;</button>';
                html += '</div>';
            });
            list.innerHTML = html;
        }

        async function addReminder() {
            const timeVal = document.getElementById('reminder-time').value;
            const parts = timeVal.split(':');
            const hour = parseInt(parts[0]) || 0;
            const minute = parseInt(parts[1]) || 0;
            const message = document.getElementById('reminder-message').value.trim();
            const recurring = document.getElementById('reminder-recurring').checked;

            if (!message) { showToast('Enter a message', 'error'); return; }

            try {
                const resp = await fetch('/api/reminders', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ hour, minute, message, recurring })
                });
                const data = await resp.json();
                if (data.success) {
                    showToast('Reminder added for ' + hour.toString().padStart(2,'0') + ':' + minute.toString().padStart(2,'0'));
                    document.getElementById('reminder-message').value = '';
                    document.getElementById('reminder-char-count').textContent = '0/48';
                    loadData();
                } else {
                    showToast(data.error || 'Failed', 'error');
                }
            } catch (e) { showToast('Failed to add reminder', 'error'); }
        }

        async function deleteReminder(index) {
            try {
                await fetch('/api/reminders/delete', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ index })
                });
                showToast('Reminder deleted');
                loadData();
            } catch (e) { showToast('Failed to delete', 'error'); }
        }

        const reminderMsgEl = document.getElementById('reminder-message');
        if (reminderMsgEl) reminderMsgEl.addEventListener('input', (e) => {
            document.getElementById('reminder-char-count').textContent = e.target.value.length + '/48';
        });

        // Countdown Timer
        function updateTimerUI(timer) {
            const timeEl = document.getElementById('timer-time');
            const stateEl = document.getElementById('timer-state');
            const startBtn = document.getElementById('btn-timer-start');
            const stopBtn = document.getElementById('btn-timer-stop');

            if (timer && timer.active) {
                const m = Math.floor(timer.remainingSeconds / 60);
                const s = timer.remainingSeconds % 60;
                timeEl.textContent = m + ':' + s.toString().padStart(2, '0');
                stateEl.textContent = timer.name || 'Running';
                startBtn.classList.add('hidden');
                stopBtn.classList.remove('hidden');
            } else {
                timeEl.textContent = '--:--';
                stateEl.textContent = 'Ready';
                startBtn.classList.remove('hidden');
                stopBtn.classList.add('hidden');
            }
        }

        async function startCountdown() {
            const minutes = parseInt(document.getElementById('timerMinutes').value) || 5;
            try {
                await fetch('/api/timer/start', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ minutes: minutes })
                });
                showToast('Timer started: ' + minutes + ' min');
                loadData();
            } catch (e) { showToast('Failed to start timer', 'error'); }
        }

        async function stopCountdown() {
            try {
                await fetch('/api/timer/stop', { method: 'POST' });
                showToast('Timer stopped');
                loadData();
            } catch (e) { showToast('Failed to stop timer', 'error'); }
        }

        // Timer slider
        const timerMinEl = document.getElementById('timerMinutes');
        if (timerMinEl) timerMinEl.addEventListener('input', (e) => {
            document.getElementById('timerMinutes-val').textContent = e.target.value + ' min';
        });

        // Timer ticking toggle
        const timerTickEl = document.getElementById('timerTickingEnabled');
        if (timerTickEl) timerTickEl.addEventListener('change', (e) => {
            fetch('/api/settings', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ timerTickingEnabled: e.target.checked })
            });
        });

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

        // OTA Upload
        const otaDropzone = document.getElementById('ota-dropzone');
        const otaFileInput = document.getElementById('ota-file');
        const otaProgress = document.getElementById('ota-progress');
        const otaFill = document.getElementById('ota-fill');
        const otaStatus = document.getElementById('ota-status');

        otaDropzone.addEventListener('click', () => otaFileInput.click());
        otaDropzone.addEventListener('dragover', (e) => {
            e.preventDefault();
            otaDropzone.classList.add('dragover');
        });
        otaDropzone.addEventListener('dragleave', () => {
            otaDropzone.classList.remove('dragover');
        });
        otaDropzone.addEventListener('drop', (e) => {
            e.preventDefault();
            otaDropzone.classList.remove('dragover');
            if (e.dataTransfer.files.length > 0) {
                uploadFirmware(e.dataTransfer.files[0]);
            }
        });
        otaFileInput.addEventListener('change', (e) => {
            if (e.target.files.length > 0) {
                uploadFirmware(e.target.files[0]);
            }
        });

        async function uploadFirmware(file) {
            if (!file.name.endsWith('.bin')) {
                showToast('Please select a .bin file', 'error');
                return;
            }
            if (!confirm('Upload ' + file.name + ' (' + (file.size / 1024 / 1024).toFixed(2) + ' MB)?\\n\\nThe device will restart after update.')) {
                return;
            }

            otaDropzone.classList.add('hidden');
            otaProgress.classList.remove('hidden');
            otaFill.style.width = '0%';
            otaStatus.textContent = 'Uploading...';
            otaStatus.className = 'ota-status';

            try {
                const response = await fetch('/api/ota/upload', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/octet-stream' },
                    body: file
                });

                if (response.ok) {
                    otaFill.style.width = '100%';
                    otaStatus.textContent = 'Update complete! Restarting...';
                    otaStatus.classList.add('ota-success');
                } else {
                    const err = await response.text();
                    throw new Error(err || 'Upload failed');
                }
            } catch (e) {
                otaStatus.textContent = 'Error: ' + e.message;
                otaStatus.classList.add('ota-error');
                setTimeout(() => {
                    otaDropzone.classList.remove('hidden');
                    otaProgress.classList.add('hidden');
                }, 3000);
            }
        }

        async function loadSystemInfo() {
            try {
                const info = await fetch('/api/system/info').then(r => r.json());
                document.getElementById('sys-version').textContent = info.version || '--';
                document.getElementById('sys-build').textContent = info.buildDate || '--';
                document.getElementById('sys-partition').textContent = info.partitionLabel || '--';
                document.getElementById('sys-heap').textContent = info.freeHeap ? Math.round(info.freeHeap / 1024) + ' KB' : '--';
                document.getElementById('sys-notes').textContent = info.releaseNotes || 'No release notes';
                document.getElementById('btn-rollback').disabled = !info.canRollback;
            } catch (e) {
                console.error('Failed to load system info', e);
            }
        }

        async function restartDevice() {
            if (!confirm('Restart the device?')) return;
            try {
                await fetch('/api/system/restart', { method: 'POST' });
                showToast('Restarting...');
            } catch (e) {
                showToast('Failed to restart', 'error');
            }
        }

        async function rollbackFirmware() {
            if (!confirm('Rollback to previous firmware?\\n\\nThe device will restart.')) return;
            try {
                await fetch('/api/system/rollback', { method: 'POST' });
                showToast('Rolling back...');
            } catch (e) {
                showToast('Rollback failed', 'error');
            }
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

        // Breathing/Wellness
        // Populate breathing hour selects
        const breathingStartSel = document.getElementById('breathing-start-hour');
        const breathingEndSel = document.getElementById('breathing-end-hour');
        for (let i = 0; i < 24; i++) {
            const optStart = document.createElement('option');
            optStart.value = i;
            optStart.textContent = i.toString().padStart(2, '0') + ':00';
            breathingStartSel.appendChild(optStart);

            const optEnd = document.createElement('option');
            optEnd.value = i;
            optEnd.textContent = i.toString().padStart(2, '0') + ':00';
            breathingEndSel.appendChild(optEnd);
        }
        // Set defaults
        breathingStartSel.value = 9;
        breathingEndSel.value = 17;

        // Breathing interval slider
        const breathingIntervalEl = document.getElementById('breathing-interval');
        breathingIntervalEl.addEventListener('input', (e) => {
            document.getElementById('breathing-interval-val').textContent = e.target.value + ' min';
            updateBreathingSetting('breathingIntervalMinutes', parseInt(e.target.value));
        });

        // Breathing enabled toggle
        document.getElementById('breathing-enabled').addEventListener('change', (e) => {
            updateBreathingSetting('breathingEnabled', e.target.checked);
            document.getElementById('breathing-schedule-card').style.opacity = e.target.checked ? '1' : '0.5';
        });

        // Breathing sound toggle
        document.getElementById('breathing-sound-enabled').addEventListener('change', (e) => {
            updateBreathingSetting('breathingSoundEnabled', e.target.checked);
        });

        // Breathing hour selects
        breathingStartSel.addEventListener('change', () => {
            updateBreathingSetting('breathingStartHour', parseInt(breathingStartSel.value));
        });
        breathingEndSel.addEventListener('change', () => {
            updateBreathingSetting('breathingEndHour', parseInt(breathingEndSel.value));
        });

        let breathingUpdateTimeout;
        function updateBreathingSetting(key, value) {
            clearTimeout(breathingUpdateTimeout);
            breathingUpdateTimeout = setTimeout(() => {
                fetch('/api/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ [key]: value })
                });
            }, 300);
        }

        async function startBreathing() {
            try {
                await fetch('/api/breathing/start', { method: 'POST' });
                showToast('Breathing exercise started');
            } catch (e) {
                showToast('Failed to start breathing', 'error');
            }
        }

        // ============== ASSISTANT FUNCTIONS ==============

        async function loadAssistantStatus() {
            try {
                const resp = await fetch('/api/assistant/status');
                if (resp.ok) {
                    const data = await resp.json();
                    document.getElementById('assistant-state').textContent = data.state || 'Idle';
                    document.getElementById('assistant-tokens').textContent =
                        (data.contextTokens || 0) + ' / 8000';
                }
            } catch (e) { /* ignore */ }
        }

        async function clearAssistantHistory() {
            if (!confirm('Clear conversation history?')) return;
            try {
                await fetch('/api/assistant/clear', { method: 'POST' });
                showToast('History cleared');
                loadAssistantStatus();
            } catch (e) {
                showToast('Failed to clear history', 'error');
            }
        }

        async function loadMcpServers() {
            try {
                const resp = await fetch('/api/mcp/servers');
                if (resp.ok) {
                    const data = await resp.json();
                    updateMcpServersList(data.servers || []);
                    const host = location.hostname;
                    document.getElementById('mcp-endpoint').textContent =
                        'http://' + host + ':3001/sse';
                    const config = JSON.stringify({
                        "deskbuddy": {
                            "command": "npx",
                            "args": ["-y", "mcp-remote", "http://" + host + ":3001/sse", "--allow-http"]
                        }
                    }, null, 2);
                    document.getElementById('mcp-claude-config').textContent = config;
                }
            } catch (e) { /* ignore */ }
        }

        function copyMcpConfig() {
            const text = document.getElementById('mcp-claude-config').textContent;
            navigator.clipboard.writeText(text).then(() => {
                const btn = document.getElementById('mcp-copy-btn');
                btn.textContent = 'Copied!';
                setTimeout(() => btn.textContent = 'Copy', 2000);
            });
        }

        function updateMcpServersList(servers) {
            const container = document.getElementById('mcp-servers-list');
            if (servers.length === 0) {
                container.innerHTML = '<p style="color: var(--muted-foreground); font-style: italic;">No MCP servers configured</p>';
                return;
            }
            container.innerHTML = servers.map((s, i) => `
                <div class="status-row" style="align-items: flex-start;">
                    <div>
                        <span class="status-dot" style="display: inline-block; width: 8px; height: 8px; border-radius: 50%; margin-right: 8px; background: ${s.connected ? 'var(--status-active)' : 'var(--destructive)'};"></span>
                        <strong>${s.name}</strong>
                        <div style="color: var(--muted-foreground); font-size: 12px; margin-left: 16px;">${s.url}</div>
                        ${s.toolCount ? '<div style="color: var(--muted-foreground); font-size: 12px; margin-left: 16px;">' + s.toolCount + ' tools</div>' : ''}
                        ${s.error ? '<div style="color: var(--destructive); font-size: 12px; margin-left: 16px;">' + s.error + '</div>' : ''}
                    </div>
                    <div style="display: flex; gap: 8px;">
                        <button class="btn btn-secondary" onclick="editMcpServer(${i})" style="padding: 4px 12px;">Edit</button>
                        <button class="btn btn-danger" onclick="deleteMcpServer(${i})" style="padding: 4px 12px;">Delete</button>
                    </div>
                </div>
            `).join('');
        }

        function showAddMcpServer() {
            document.getElementById('mcp-modal-title').textContent = 'Add MCP Server';
            document.getElementById('mcp-name').value = '';
            document.getElementById('mcp-url').value = '';
            document.getElementById('mcp-apikey').value = '';
            document.getElementById('mcp-edit-index').value = '-1';
            document.getElementById('mcp-modal').style.display = 'flex';
        }

        async function editMcpServer(index) {
            try {
                const resp = await fetch('/api/mcp/servers');
                const data = await resp.json();
                const server = data.servers[index];
                if (server) {
                    document.getElementById('mcp-modal-title').textContent = 'Edit MCP Server';
                    document.getElementById('mcp-name').value = server.name;
                    document.getElementById('mcp-url').value = server.url;
                    document.getElementById('mcp-apikey').value = '';
                    document.getElementById('mcp-edit-index').value = index;
                    document.getElementById('mcp-modal').style.display = 'flex';
                }
            } catch (e) {
                showToast('Failed to load server', 'error');
            }
        }

        function closeMcpModal() {
            document.getElementById('mcp-modal').style.display = 'none';
        }

        async function saveMcpServer() {
            const name = document.getElementById('mcp-name').value.trim();
            const url = document.getElementById('mcp-url').value.trim();
            const apiKey = document.getElementById('mcp-apikey').value;
            const editIndex = parseInt(document.getElementById('mcp-edit-index').value);

            if (!name || !url) {
                showToast('Name and URL required', 'error');
                return;
            }

            try {
                const endpoint = editIndex >= 0 ? '/api/mcp/servers/' + editIndex : '/api/mcp/servers';
                const method = editIndex >= 0 ? 'PUT' : 'POST';
                await fetch(endpoint, {
                    method: method,
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ name, url, apiKey })
                });
                closeMcpModal();
                showToast('Server saved');
                loadMcpServers();
            } catch (e) {
                showToast('Failed to save server', 'error');
            }
        }

        async function deleteMcpServer(index) {
            if (!confirm('Delete this MCP server?')) return;
            try {
                await fetch('/api/mcp/servers/' + index, { method: 'DELETE' });
                showToast('Server deleted');
                loadMcpServers();
            } catch (e) {
                showToast('Failed to delete server', 'error');
            }
        }

        async function discoverMcpTools() {
            try {
                showToast('Discovering tools...');
                await fetch('/api/mcp/discover', { method: 'POST' });
                loadMcpServers();
                showToast('Tools refreshed');
            } catch (e) {
                showToast('Discovery failed', 'error');
            }
        }

        function updateLlmKeyPlaceholder() {
            const provider = document.getElementById('llm-provider').value;
            const label = document.getElementById('llm-key-label');
            const input = document.getElementById('llm-api-key');
            if (provider === 'openai') {
                label.textContent = 'OpenAI API Key';
                input.placeholder = 'sk-...';
            } else {
                label.textContent = 'Claude API Key';
                input.placeholder = 'sk-ant-...';
            }
        }

        async function testLlmApi() {
            const key = document.getElementById('llm-api-key').value;
            const provider = document.getElementById('llm-provider').value;
            try {
                const resp = await fetch('/api/assistant/test/llm', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ apiKey: key, provider })
                });
                const data = await resp.json();
                showToast(data.success ? 'LLM API OK' : 'LLM API failed: ' + data.error, data.success ? 'success' : 'error');
            } catch (e) {
                showToast('Test failed', 'error');
            }
        }

        async function testVoiceApi() {
            const key = document.getElementById('openai-voice-key').value;
            try {
                const resp = await fetch('/api/assistant/test/voice', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ apiKey: key })
                });
                const data = await resp.json();
                showToast(data.success ? 'Voice API OK' : 'Voice API failed: ' + data.error, data.success ? 'success' : 'error');
            } catch (e) {
                showToast('Test failed', 'error');
            }
        }

        async function previewVoice() {
            const voice = document.getElementById('tts-voice').value;
            try {
                await fetch('/api/assistant/preview-voice', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ voice })
                });
                showToast('Playing voice preview');
            } catch (e) {
                showToast('Preview failed', 'error');
            }
        }

        async function saveAssistantSettings() {
            const settings = {
                llmProvider: document.getElementById('llm-provider').value,
                llmApiKey: document.getElementById('llm-api-key').value,
                openaiVoiceKey: document.getElementById('openai-voice-key').value,
                ttsVoice: document.getElementById('tts-voice').value,
                ttsSpeed: parseFloat(document.getElementById('tts-speed').value),
                wakeWordEnabled: document.getElementById('wake-word-enabled').checked,
                pttEnabled: document.getElementById('ptt-enabled').checked,
                wakeSensitivity: parseInt(document.getElementById('wake-sensitivity').value),
                systemPrompt: document.getElementById('system-prompt').value
            };
            try {
                await fetch('/api/assistant/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(settings)
                });
                showToast('Assistant settings saved');
            } catch (e) {
                showToast('Failed to save settings', 'error');
            }
        }

        async function loadAssistantSettings() {
            try {
                const resp = await fetch('/api/assistant/settings');
                if (resp.ok) {
                    const data = await resp.json();
                    if (data.llmProvider) {
                        document.getElementById('llm-provider').value = data.llmProvider;
                        updateLlmKeyPlaceholder();
                    }
                    if (data.llmApiKey) document.getElementById('llm-api-key').value = data.llmApiKey;
                    if (data.openaiVoiceKey) document.getElementById('openai-voice-key').value = data.openaiVoiceKey;
                    if (data.ttsVoice) document.getElementById('tts-voice').value = data.ttsVoice;
                    if (data.ttsSpeed) {
                        document.getElementById('tts-speed').value = data.ttsSpeed;
                        document.getElementById('tts-speed-val').textContent = data.ttsSpeed + 'x';
                    }
                    if (data.wakeWordEnabled !== undefined) document.getElementById('wake-word-enabled').checked = data.wakeWordEnabled;
                    if (data.pttEnabled !== undefined) document.getElementById('ptt-enabled').checked = data.pttEnabled;
                    if (data.wakeSensitivity !== undefined) {
                        document.getElementById('wake-sensitivity').value = data.wakeSensitivity;
                        document.getElementById('wake-sensitivity-val').textContent = data.wakeSensitivity + '%';
                    }
                    if (data.systemPrompt) document.getElementById('system-prompt').value = data.systemPrompt;
                }
            } catch (e) { /* ignore */ }
        }

        // Assistant slider handlers
        document.getElementById('tts-speed')?.addEventListener('input', function() {
            document.getElementById('tts-speed-val').textContent = this.value + 'x';
        });
        document.getElementById('wake-sensitivity')?.addEventListener('input', function() {
            document.getElementById('wake-sensitivity-val').textContent = this.value + '%';
        });

        // Load assistant data when tab is selected
        document.querySelectorAll('.tab').forEach(tab => {
            tab.addEventListener('click', () => {
                if (tab.dataset.tab === 'assistant') {
                    loadAssistantStatus();
                    loadMcpServers();
                }
            });
        });

        // Load settings when settings accordion opens
        const origToggleAccordion = window.toggleAccordion;
        window.toggleAccordion = function(name) {
            if (name === 'assistant-settings') {
                loadAssistantSettings();
            }
            if (origToggleAccordion) origToggleAccordion(name);
        };

        // Init
        loadData();
        setInterval(loadData, 1000);
    </script>
</body>
</html>
)rawliteral";

    return html;
}
