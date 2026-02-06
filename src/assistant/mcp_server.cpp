/**
 * @file mcp_server.cpp
 * @brief MCP Server with dedicated FreeRTOS task for SSE transport
 *
 * Runs its own WiFiServer in a FreeRTOS task on a separate port (default 3001).
 * This ensures fast response times regardless of main loop timing, which is
 * critical for meeting Claude Desktop's 5-second initialization timeout.
 *
 * Protocol flow:
 * 1. Client GETs /sse -> receives endpoint event with message URL
 * 2. Client POSTs JSON-RPC to /mcp/message?sessionId=xxx
 * 3. Server sends JSON-RPC responses as SSE events on the open SSE stream
 */

#include "mcp_server.h"
#include <esp_random.h>

// Global instance
MCPServer mcpServer;

//=============================================================================
// Constructor / Destructor
//=============================================================================

MCPServer::MCPServer()
    : taskHandle(nullptr)
    , tcpServer(nullptr)
    , lastKeepAlive(0)
    , port(MCP_SERVER_PORT)
    , enabled(true)
    , running(false)
    , toolExecutor(nullptr)
{
}

MCPServer::~MCPServer() {
    end();
}

//=============================================================================
// Initialization
//=============================================================================

bool MCPServer::begin(uint16_t p) {
    if (running) return true;

    port = p;

    // Create WiFiServer with backlog of 4 pending connections
    tcpServer = new WiFiServer(port, 4);
    tcpServer->begin();
    tcpServer->setNoDelay(true);

    running = true;

    // Start dedicated FreeRTOS task for MCP server
    xTaskCreatePinnedToCore(
        serverTask,
        "mcp_server",
        MCP_TASK_STACK_SIZE,
        this,
        2,                  // Priority (slightly above idle)
        &taskHandle,
        1                   // Run on core 1 (same as Arduino loop)
    );

    Serial.printf("[MCP] SSE server started on port %d (dedicated task)\n", port);
    Serial.printf("[MCP] %d tools registered\n", tools.size());
    return true;
}

void MCPServer::end() {
    if (!running) return;
    running = false;

    // Wait for task to finish
    if (taskHandle) {
        vTaskDelay(pdMS_TO_TICKS(100));
        taskHandle = nullptr;
    }

    closeSSEConnection();
    if (tcpServer) {
        tcpServer->end();
        delete tcpServer;
        tcpServer = nullptr;
    }
    Serial.println("[MCP] Server stopped");
}

//=============================================================================
// FreeRTOS Task
//=============================================================================

void MCPServer::serverTask(void* param) {
    MCPServer* self = (MCPServer*)param;

    while (self->running) {
        // Accept and handle all pending connections
        WiFiClient client;
        while (self->running && (client = self->tcpServer->accept())) {
            self->handleNewConnection(client);
        }

        // Send keepalive on SSE connection
        if (self->sseClient && self->sseClient.connected()) {
            if (millis() - self->lastKeepAlive >= MCP_KEEPALIVE_INTERVAL_MS) {
                self->sendKeepAlive();
                self->lastKeepAlive = millis();
            }
        } else if (self->sessionId.length() > 0) {
            Serial.printf("[MCP] SSE connection lost (client=%d, connected=%d)\n",
                         (bool)self->sseClient, self->sseClient ? self->sseClient.connected() : 0);
            self->sessionId = "";
        }

        // Small delay to avoid busy-waiting
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    Serial.println("[MCP] Server task exiting");
    vTaskDelete(NULL);
}

//=============================================================================
// Tool Management
//=============================================================================

void MCPServer::addTool(const char* name, const char* description, const char* inputSchema) {
    for (const auto& t : tools) {
        if (t.name == name) return;  // Already registered
    }

    MCPTool tool;
    tool.name = name;
    tool.description = description;
    tool.inputSchema = inputSchema;
    tools.push_back(tool);
    Serial.printf("[MCP] Registered tool: %s\n", name);
}

void MCPServer::removeTool(const char* name) {
    for (auto it = tools.begin(); it != tools.end(); ++it) {
        if (it->name == name) {
            tools.erase(it);
            return;
        }
    }
}

void MCPServer::clearTools() {
    tools.clear();
}

//=============================================================================
// Connection Handling
//=============================================================================

void MCPServer::handleNewConnection(WiFiClient client) {
    client.setTimeout(1000);  // 1 second timeout for reading

    // Read request line: "GET /sse HTTP/1.1"
    String requestLine = client.readStringUntil('\n');
    requestLine.trim();

    int sp1 = requestLine.indexOf(' ');
    int sp2 = requestLine.indexOf(' ', sp1 + 1);
    if (sp1 < 0 || sp2 < 0) {
        client.stop();
        return;
    }

    String method = requestLine.substring(0, sp1);
    String uri = requestLine.substring(sp1 + 1, sp2);

    // Read headers
    int contentLength = 0;
    while (client.connected()) {
        String header = client.readStringUntil('\n');
        header.trim();
        if (header.length() == 0) break;  // Empty line = end of headers

        // Case-insensitive Content-Length check
        String headerLower = header;
        headerLower.toLowerCase();
        if (headerLower.startsWith("content-length:")) {
            contentLength = header.substring(header.indexOf(':') + 1).toInt();
        }
    }

    // Route request - only read body for message endpoint
    if (method == "GET" && uri == "/sse") {
        handleSSERequest(client);
    } else if (method == "POST" && uri.startsWith("/mcp/message")) {
        // Read body only for message requests
        String body;
        if (contentLength > 0 && contentLength < 4096) {
            char* buf = (char*)malloc(contentLength + 1);
            if (buf) {
                int bytesRead = 0;
                unsigned long start = millis();
                while (bytesRead < contentLength && client.connected() && (millis() - start < 2000)) {
                    int avail = client.available();
                    if (avail > 0) {
                        int toRead = min(avail, contentLength - bytesRead);
                        int r = client.read((uint8_t*)(buf + bytesRead), toRead);
                        if (r > 0) bytesRead += r;
                    } else {
                        vTaskDelay(1);
                    }
                }
                buf[bytesRead] = '\0';
                body = String(buf);
                free(buf);
            }
        }
        handleMessageRequest(client, uri, body);
    } else if (method == "OPTIONS") {
        // CORS preflight
        client.print(
            "HTTP/1.1 204 No Content\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
            "Access-Control-Allow-Headers: Content-Type\r\n"
            "\r\n"
        );
        client.stop();
    } else {
        Serial.printf("[MCP] 404: %s %s\n", method.c_str(), uri.c_str());
        client.print("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
        client.stop();
    }
}

//=============================================================================
// SSE Handler - GET /sse
//=============================================================================

void MCPServer::handleSSERequest(WiFiClient& client) {
    if (!enabled) {
        client.print("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n");
        client.stop();
        return;
    }

    // Close any existing SSE connection
    closeSSEConnection();

    // Generate new session ID
    sessionId = generateSessionId();
    lastKeepAlive = millis();

    Serial.printf("[MCP] SSE client connected (session=%s)\n", sessionId.c_str());

    // Send SSE headers + endpoint event
    String response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n"
        "event: endpoint\n"
        "data: /mcp/message?sessionId=" + sessionId + "\n\n";

    // Disable Nagle's algorithm so keepalives are sent immediately
    client.setNoDelay(true);

    client.print(response);
    client.flush();

    // Store client - DON'T call client.stop(), keep the connection open
    sseClient = client;
}

//=============================================================================
// Message Handler - POST /mcp/message?sessionId=xxx
//=============================================================================

void MCPServer::handleMessageRequest(WiFiClient& client, const String& uri, const String& body) {
    if (!enabled) {
        client.print("HTTP/1.1 503 Service Unavailable\r\nContent-Length: 0\r\n\r\n");
        client.stop();
        return;
    }

    // Extract sessionId from query string
    int qPos = uri.indexOf("sessionId=");
    if (qPos >= 0) {
        String paramSession = uri.substring(qPos + 10);
        int ampPos = paramSession.indexOf('&');
        if (ampPos >= 0) paramSession = paramSession.substring(0, ampPos);

        if (sessionId.length() > 0 && sessionId != paramSession) {
            client.print(
                "HTTP/1.1 400 Bad Request\r\n"
                "Content-Type: application/json\r\n"
                "Content-Length: 27\r\n"
                "Connection: close\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "\r\n"
                "{\"error\":\"Invalid session\"}"
            );
            client.stop();
            return;
        }
    }

    // Check SSE connection is alive
    if (!sseClient || !sseClient.connected()) {
        client.print(
            "HTTP/1.1 400 Bad Request\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: 28\r\n"
            "Connection: close\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "\r\n"
            "{\"error\":\"No SSE connection\"}"
        );
        client.stop();
        return;
    }

    Serial.printf("[MCP] Message: %.100s%s\n", body.c_str(), (body.length() > 100) ? "..." : "");

    // Process JSON-RPC and get response
    String response = processJsonRpc(body.c_str());

    // Send response via SSE (if there is one - notifications have no response)
    if (response.length() > 0) {
        if (!sendSSEEvent(response)) {
            Serial.println("[MCP] Failed to send SSE response");
        }
    }

    // Drain any unread data to prevent RST on close
    while (client.available()) client.read();

    // Acknowledge the POST with 202 Accepted
    client.print(
        "HTTP/1.1 202 Accepted\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: 11\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n"
        "{\"ok\":true}"
    );
    client.flush();
    client.stop();
}

//=============================================================================
// JSON-RPC Processing
//=============================================================================

String MCPServer::processJsonRpc(const char* body) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    if (error) {
        return makeErrorResponse(0, -32700, "Parse error");
    }

    const char* method = doc["method"];
    if (!method) {
        return makeErrorResponse(doc["id"] | 0, -32600, "Missing method");
    }

    int id = doc["id"] | 0;

    // Notifications (no id) - no response needed
    if (strcmp(method, "notifications/initialized") == 0) {
        Serial.println("[MCP] Client initialized");
        return "";
    }
    if (strcmp(method, "notifications/cancelled") == 0) {
        return "";
    }

    // Methods that require a response
    if (strcmp(method, "initialize") == 0) {
        return handleInitialize(id);
    }
    if (strcmp(method, "tools/list") == 0) {
        return handleToolsList(id);
    }
    if (strcmp(method, "tools/call") == 0) {
        JsonObject params = doc["params"];
        return handleToolsCall(id, params);
    }
    if (strcmp(method, "ping") == 0) {
        return handlePing(id);
    }

    return makeErrorResponse(id, -32601, "Method not found");
}

String MCPServer::handleInitialize(int id) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["id"] = id;

    JsonObject result = doc["result"].to<JsonObject>();
    result["protocolVersion"] = MCP_PROTOCOL_VERSION;

    JsonObject caps = result["capabilities"].to<JsonObject>();
    caps["tools"].to<JsonObject>();  // Empty object = tools supported

    JsonObject serverInfo = result["serverInfo"].to<JsonObject>();
    serverInfo["name"] = MCP_SERVER_NAME;
    serverInfo["version"] = MCP_SERVER_VERSION;

    String response;
    serializeJson(doc, response);

    Serial.println("[MCP] Initialize handshake complete");
    return response;
}

String MCPServer::handleToolsList(int id) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["id"] = id;

    JsonArray toolsArray = doc["result"]["tools"].to<JsonArray>();

    for (const auto& tool : tools) {
        JsonObject t = toolsArray.add<JsonObject>();
        t["name"] = tool.name;
        t["description"] = tool.description;

        // Parse schema string into JSON object
        JsonDocument schemaDoc;
        deserializeJson(schemaDoc, tool.inputSchema);
        t["inputSchema"] = schemaDoc;
    }

    String response;
    serializeJson(doc, response);

    Serial.printf("[MCP] Listed %d tools\n", tools.size());
    return response;
}

String MCPServer::handleToolsCall(int id, JsonObject& params) {
    const char* toolName = params["name"];
    if (!toolName) {
        return makeErrorResponse(id, -32602, "Missing tool name");
    }

    // Verify tool exists
    bool found = false;
    for (const auto& t : tools) {
        if (t.name == toolName) { found = true; break; }
    }
    if (!found) {
        return makeErrorResponse(id, -32602, "Unknown tool");
    }

    // Get arguments
    String arguments;
    if (params["arguments"].is<JsonObject>()) {
        serializeJson(params["arguments"], arguments);
    } else {
        arguments = "{}";
    }

    // Execute tool
    String result;
    if (toolExecutor) {
        result = toolExecutor(toolName, arguments);
    } else {
        result = "{\"error\":\"No tool executor configured\"}";
    }

    // Build MCP response
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["id"] = id;

    JsonArray content = doc["result"]["content"].to<JsonArray>();
    JsonObject textBlock = content.add<JsonObject>();
    textBlock["type"] = "text";
    textBlock["text"] = result;

    String response;
    serializeJson(doc, response);

    Serial.printf("[MCP] Tool called: %s\n", toolName);
    return response;
}

String MCPServer::handlePing(int id) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["id"] = id;
    doc["result"] = JsonObject();

    String response;
    serializeJson(doc, response);
    return response;
}

String MCPServer::makeErrorResponse(int id, int code, const char* message) {
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["id"] = id;
    doc["error"]["code"] = code;
    doc["error"]["message"] = message;

    String response;
    serializeJson(doc, response);
    return response;
}

//=============================================================================
// SSE Helpers
//=============================================================================

bool MCPServer::sendSSEEvent(const String& json) {
    if (!sseClient || !sseClient.connected()) return false;

    String event = "event: message\ndata: " + json + "\n\n";

    size_t written = sseClient.print(event);
    if (written == 0) {
        Serial.println("[MCP] SSE send failed - client disconnected?");
        closeSSEConnection();
        return false;
    }

    sseClient.flush();
    return true;
}

void MCPServer::sendKeepAlive() {
    if (!sseClient || !sseClient.connected()) return;

    size_t written = sseClient.print(": keepalive\n\n");
    if (written == 0) {
        Serial.println("[MCP] Keepalive failed - closing SSE");
        closeSSEConnection();
        return;
    }
    sseClient.flush();
}

void MCPServer::closeSSEConnection() {
    if (sseClient && sseClient.connected()) {
        Serial.println("[MCP] Closing SSE connection");
        sseClient.stop();
    }
    sessionId = "";
}

String MCPServer::generateSessionId() {
    char id[33];
    for (int i = 0; i < 32; i++) {
        id[i] = "0123456789abcdef"[esp_random() % 16];
    }
    id[32] = '\0';
    return String(id);
}
