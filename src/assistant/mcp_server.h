/**
 * @file mcp_server.h
 * @brief MCP Server with SSE transport for DeskBuddy
 *
 * Exposes DeskBuddy tools via the Model Context Protocol using
 * the legacy HTTP+SSE transport (compatible with mcp-remote).
 *
 * Runs a dedicated TCP server in its own FreeRTOS task on a separate
 * port (default 3001). This ensures fast response times regardless
 * of main loop timing.
 *
 * Transport:
 * - GET  /sse             - SSE stream (sends endpoint event, keeps alive)
 * - POST /mcp/message     - JSON-RPC messages from client
 */

#ifndef MCP_SERVER_H
#define MCP_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <functional>
#include <vector>

//=============================================================================
// MCP Protocol Constants
//=============================================================================

#define MCP_PROTOCOL_VERSION "2024-11-05"
#define MCP_SERVER_NAME "DeskBuddy"
#define MCP_SERVER_VERSION "1.0.0"

/** Keepalive interval in milliseconds */
#define MCP_KEEPALIVE_INTERVAL_MS 15000

/** Default TCP port for MCP SSE server */
#define MCP_SERVER_PORT 3001

/** Stack size for MCP server task */
#define MCP_TASK_STACK_SIZE 8192

//=============================================================================
// Tool Definition
//=============================================================================

struct MCPTool {
    String name;
    String description;
    String inputSchema;  ///< JSON schema string
};

using MCPToolExecutor = std::function<String(const String& toolName, const String& arguments)>;

//=============================================================================
// MCPServer Class
//=============================================================================

class MCPServer {
public:
    MCPServer();
    ~MCPServer();

    /**
     * @brief Start the MCP SSE server in its own FreeRTOS task
     * @param port TCP port to listen on (default 3001)
     * @return true if server started successfully
     */
    bool begin(uint16_t port = MCP_SERVER_PORT);

    void end();

    /**
     * @brief No-op - kept for API compatibility. Server runs in its own task.
     */
    void update() {}

    //-------------------------------------------------------------------------
    // Tool Management
    //-------------------------------------------------------------------------

    void addTool(const char* name, const char* description, const char* inputSchema);
    void removeTool(const char* name);
    void clearTools();
    void setToolExecutor(MCPToolExecutor executor) { toolExecutor = executor; }

    //-------------------------------------------------------------------------
    // State
    //-------------------------------------------------------------------------

    bool isEnabled() const { return enabled; }
    void setEnabled(bool enable) { enabled = enable; }
    int getToolCount() const { return tools.size(); }
    bool hasSSEClient() { return sseClient && sseClient.connected(); }
    uint16_t getPort() const { return port; }

private:
    //-------------------------------------------------------------------------
    // FreeRTOS Task
    //-------------------------------------------------------------------------

    static void serverTask(void* param);
    TaskHandle_t taskHandle;

    //-------------------------------------------------------------------------
    // Connection Handling
    //-------------------------------------------------------------------------

    void handleNewConnection(WiFiClient client);
    void handleSSERequest(WiFiClient& client);
    void handleMessageRequest(WiFiClient& client, const String& queryString, const String& body);

    //-------------------------------------------------------------------------
    // JSON-RPC Method Handlers
    //-------------------------------------------------------------------------

    String processJsonRpc(const char* body);
    String handleInitialize(int id);
    String handleToolsList(int id);
    String handleToolsCall(int id, JsonObject& params);
    String handlePing(int id);
    String makeErrorResponse(int id, int code, const char* message);

    //-------------------------------------------------------------------------
    // SSE Helpers
    //-------------------------------------------------------------------------

    bool sendSSEEvent(const String& json);
    void sendKeepAlive();
    void closeSSEConnection();

    static String generateSessionId();

    WiFiServer* tcpServer;
    WiFiClient sseClient;
    String sessionId;
    uint32_t lastKeepAlive;
    uint16_t port;
    bool enabled;
    volatile bool running;

    // Tools
    std::vector<MCPTool> tools;
    MCPToolExecutor toolExecutor;
};

// Global MCP server instance
extern MCPServer mcpServer;

#endif // MCP_SERVER_H
