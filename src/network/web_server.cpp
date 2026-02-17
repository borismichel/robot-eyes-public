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
#include "web_ui_html.h"
#include "wifi_manager.h"
#include "ota_manager.h"
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include "../ui/settings_menu.h"
#include "../ui/pomodoro.h"
#include "../ui/countdown_timer.h"
#include "../ui/reminder_manager.h"
#include "../behavior/breathing_exercise.h"
#include "../assistant/mcp_client.h"
#include "../assistant/mcp_server.h"
#include "../assistant/device_tools.h"
#include "../assistant/assistant.h"
#include "../audio/i2s_duplex.h"
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
    config.max_uri_handlers = 40;  // 36 handlers + headroom
    config.stack_size = 16384;  // Needs room for outbound HTTPS (API key tests, OTA)

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

    httpd_uri_t getSavedNetworksUri = {
        .uri = "/api/wifi/networks",
        .method = HTTP_GET,
        .handler = handleGetSavedNetworks,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &getSavedNetworksUri);

    httpd_uri_t removeNetworkUri = {
        .uri = "/api/wifi/remove",
        .method = HTTP_POST,
        .handler = handleRemoveNetwork,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &removeNetworkUri);

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

    httpd_uri_t editReminderUri = {
        .uri = "/api/reminders/edit",
        .method = HTTP_POST,
        .handler = handleEditReminder,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &editReminderUri);

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

    httpd_uri_t testLlmUri = {
        .uri = "/api/assistant/test/llm",
        .method = HTTP_POST,
        .handler = handleTestLlmApi,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &testLlmUri);

    httpd_uri_t testVoiceUri = {
        .uri = "/api/assistant/test/voice",
        .method = HTTP_POST,
        .handler = handleTestVoiceApi,
        .user_ctx = this
    };
    httpd_register_uri_handler(server, &testVoiceUri);

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
        if (doc["breathingExerciseType"].is<int>()) {
            self->breathingExercise->setExerciseType((BreathingType)doc["breathingExerciseType"].as<int>());
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

esp_err_t WebServerManager::handleEditReminder(httpd_req_t* req) {
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

    int index = doc["index"] | -1;
    int hour = doc["hour"] | 0;
    int minute = doc["minute"] | 0;
    const char* message = doc["message"] | "";
    bool recurring = doc["recurring"] | false;

    bool ok = false;
    if (self->reminderManager && index >= 0) {
        ok = self->reminderManager->edit(index, hour, minute, message, recurring);
    }

    httpd_resp_set_type(req, "application/json");
    if (ok) {
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else {
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"Failed to edit reminder\"}");
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

esp_err_t WebServerManager::handleGetSavedNetworks(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    if (self->wifiManager) {
        String currentSSID = self->wifiManager->isConnected() ? self->wifiManager->getSSID() : "";
        for (const auto& net : self->wifiManager->getSavedNetworks()) {
            JsonObject obj = arr.add<JsonObject>();
            obj["ssid"] = net.ssid;
            obj["connected"] = (currentSSID == net.ssid);
        }
    }

    String json;
    serializeJson(doc, json);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json.c_str(), json.length());
    return ESP_OK;
}

esp_err_t WebServerManager::handleRemoveNetwork(httpd_req_t* req) {
    WebServerManager* self = getInstance(req);

    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    JsonDocument doc;
    if (deserializeJson(doc, buf) || !doc.containsKey("ssid")) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    String ssid = doc["ssid"].as<String>();
    if (self->wifiManager) {
        self->wifiManager->removeNetwork(ssid);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
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
    doc["ttsSpeed"] = prefs.getFloat("ttsSpeed", 0.85);
    doc["sttLanguage"] = prefs.getString("sttLang", "");
    doc["wakeWordEnabled"] = prefs.getBool("wakeWord", true);
    doc["pttEnabled"] = prefs.getBool("ptt", true);
    doc["wakeSensitivity"] = prefs.getInt("wakeSens", 50);
    doc["systemPrompt"] = prefs.getString("sysPrompt", "You are DeskBuddy, a friendly desk companion. Reply in the user's language. Keep ALL responses to 1-2 short sentences — you speak aloud through a tiny speaker. No markdown or emojis.");

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
    if (doc["llmApiKey"].is<const char*>()) {
        String key = doc["llmApiKey"].as<String>();
        if (key.length() > 0 && key != "********") {
            prefs.putString("llmKey", key);
        }
    }
    if (doc["openaiVoiceKey"].is<const char*>()) {
        String key = doc["openaiVoiceKey"].as<String>();
        if (key.length() > 0 && key != "********") {
            prefs.putString("voiceKey", key);
        }
    }
    if (doc["ttsVoice"].is<const char*>()) {
        prefs.putString("ttsVoice", doc["ttsVoice"].as<String>());
    }
    if (doc["ttsSpeed"].is<float>()) {
        prefs.putFloat("ttsSpeed", doc["ttsSpeed"].as<float>());
    }
    if (doc["sttLanguage"].is<const char*>()) {
        prefs.putString("sttLang", doc["sttLanguage"].as<String>());
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

    // Hot-reload: rebuild config from NVS and apply to running assistant
    {
        extern Assistant assistant;
        Preferences reloadPrefs;
        reloadPrefs.begin("assistant", true);

        AssistantConfig cfg;
        String llmProv = reloadPrefs.getString("llmProv", "claude");
        cfg.llmProvider = (llmProv == "openai") ? LLMProvider::OpenAI : LLMProvider::Claude;
        String llmKey = reloadPrefs.getString("llmKey", "");
        strncpy(cfg.llmApiKey, llmKey.c_str(), sizeof(cfg.llmApiKey) - 1);
        String voiceKey = reloadPrefs.getString("voiceKey", "");
        strncpy(cfg.openaiVoiceKey, voiceKey.c_str(), sizeof(cfg.openaiVoiceKey) - 1);
        String ttsVoice = reloadPrefs.getString("ttsVoice", "alloy");
        strncpy(cfg.voiceConfig.openAIVoice, ttsVoice.c_str(), sizeof(cfg.voiceConfig.openAIVoice) - 1);
        cfg.voiceConfig.speed = reloadPrefs.getFloat("ttsSpeed", 0.85f);
        String sttLang = reloadPrefs.getString("sttLang", "");
        strncpy(cfg.sttLanguage, sttLang.c_str(), sizeof(cfg.sttLanguage) - 1);
        String sysPrompt = reloadPrefs.getString("sysPrompt", "You are DeskBuddy, a friendly desk companion. Reply in the user's language. Keep ALL responses to 1-2 short sentences — you speak aloud through a tiny speaker. No markdown or emojis.");
        strncpy(cfg.systemPrompt, sysPrompt.c_str(), sizeof(cfg.systemPrompt) - 1);

        reloadPrefs.end();

        if (strlen(cfg.llmApiKey) > 0 && strlen(cfg.openaiVoiceKey) > 0) {
            if (assistant.isEnabled()) {
                assistant.setConfig(cfg);
                Serial.println("[WebServer] Assistant config updated (hot-reload)");
            } else {
                extern void setupAssistantIntegration();
                if (assistant.begin(cfg)) {
                    setupAssistantIntegration();
                    Serial.println("[WebServer] Assistant initialized from web settings");
                }
            }
        }
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true,\"restart\":false}");
    return ESP_OK;
}

esp_err_t WebServerManager::handleTestLlmApi(httpd_req_t* req) {
    char buf[512];
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

    const char* apiKey = doc["apiKey"] | "";
    const char* provider = doc["provider"] | "claude";
    if (strlen(apiKey) == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"No API key provided\"}");
        return ESP_OK;
    }

    NetworkClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(15000);

    bool success = false;
    String errorMsg;

    if (strcmp(provider, "openai") == 0) {
        // Test OpenAI: minimal chat completion
        http.begin(client, "https://api.openai.com/v1/chat/completions");
        http.addHeader("Authorization", String("Bearer ") + apiKey);
        http.addHeader("Content-Type", "application/json");
        int code = http.POST("{\"model\":\"gpt-4o\",\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}],\"max_tokens\":1}");
        if (code == 200) {
            success = true;
        } else {
            String body = http.getString();
            JsonDocument errDoc;
            if (!deserializeJson(errDoc, body) && errDoc["error"]["message"].is<const char*>()) {
                errorMsg = errDoc["error"]["message"].as<String>();
            } else {
                errorMsg = "HTTP " + String(code);
            }
        }
    } else {
        // Test Claude: minimal messages request
        http.begin(client, "https://api.anthropic.com/v1/messages");
        http.addHeader("x-api-key", apiKey);
        http.addHeader("anthropic-version", "2023-06-01");
        http.addHeader("Content-Type", "application/json");
        int code = http.POST("{\"model\":\"claude-sonnet-4-20250514\",\"max_tokens\":1,\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}]}");
        if (code == 200) {
            success = true;
        } else {
            String body = http.getString();
            JsonDocument errDoc;
            if (!deserializeJson(errDoc, body) && errDoc["error"]["message"].is<const char*>()) {
                errorMsg = errDoc["error"]["message"].as<String>();
            } else {
                errorMsg = "HTTP " + String(code);
            }
        }
    }
    http.end();

    JsonDocument resp;
    resp["success"] = success;
    if (!success) resp["error"] = errorMsg;
    String response;
    serializeJson(resp, response);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response.c_str());
    return ESP_OK;
}

esp_err_t WebServerManager::handleTestVoiceApi(httpd_req_t* req) {
    char buf[512];
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

    const char* apiKey = doc["apiKey"] | "";
    if (strlen(apiKey) == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"success\":false,\"error\":\"No API key provided\"}");
        return ESP_OK;
    }

    // Test OpenAI voice key by listing models (lightweight GET)
    NetworkClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setTimeout(15000);
    http.begin(client, "https://api.openai.com/v1/models/whisper-1");
    http.addHeader("Authorization", String("Bearer ") + apiKey);

    int code = http.GET();
    bool success = (code == 200);
    String errorMsg;
    if (!success) {
        String body = http.getString();
        JsonDocument errDoc;
        if (!deserializeJson(errDoc, body) && errDoc["error"]["message"].is<const char*>()) {
            errorMsg = errDoc["error"]["message"].as<String>();
        } else {
            errorMsg = "HTTP " + String(code);
        }
    }
    http.end();

    JsonDocument resp;
    resp["success"] = success;
    if (!success) resp["error"] = errorMsg;
    String response;
    serializeJson(resp, response);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, response.c_str());
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
        breathing["exerciseType"] = (int)breathingExercise->getExerciseType();
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
        const char* failReason = wifiManager->getFailReason();
        if (failReason[0]) {
            wifi["failReason"] = failReason;
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

    // Mic level (non-consuming — returns last computed value)
    {
        I2SDuplex& i2s = I2SDuplex::getInstance();
        doc["micLevel"] = i2s.getLastMicLevel();
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
    return String(WEB_UI_HTML);
}

