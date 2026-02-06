/**
 * @file mcp_client.cpp
 * @brief MCP Client implementation
 */

#include "mcp_client.h"
#include <Preferences.h>
#include <NetworkClientSecure.h>

// Global instance
MCPClient mcpClient;

// Preferences namespace
static const char* PREFS_NAMESPACE = "mcp_client";

//=============================================================================
// Constructor / Destructor
//=============================================================================

MCPClient::MCPClient()
    : initialized(false)
{
}

MCPClient::~MCPClient() {
    end();
}

//=============================================================================
// Initialization
//=============================================================================

bool MCPClient::begin() {
    if (initialized) return true;

    // Load saved server configurations
    loadConfig();

    initialized = true;
    Serial.println("[MCP Client] Initialized");
    return true;
}

void MCPClient::end() {
    if (!initialized) return;

    saveConfig();
    servers.clear();
    tools.clear();

    initialized = false;
    Serial.println("[MCP Client] Shutdown");
}

//=============================================================================
// Server Management
//=============================================================================

int MCPClient::addServer(const char* name, const char* url, const char* apiKey) {
    if (servers.size() >= MCP_MAX_SERVERS) {
        Serial.println("[MCP Client] Max servers reached");
        return -1;
    }

    MCPServerConfig config;
    config.name = name;
    config.url = url;
    if (apiKey) config.apiKey = apiKey;
    config.enabled = true;
    config.connected = false;

    servers.push_back(config);
    int index = servers.size() - 1;

    Serial.printf("[MCP Client] Added server: %s (%s)\n", name, url);
    return index;
}

void MCPClient::removeServer(int index) {
    if (index < 0 || index >= (int)servers.size()) return;

    // Remove tools from this server
    for (auto it = tools.begin(); it != tools.end(); ) {
        if (it->serverIndex == index) {
            it = tools.erase(it);
        } else {
            // Update server indices for tools from later servers
            if (it->serverIndex > index) {
                it->serverIndex--;
            }
            ++it;
        }
    }

    String name = servers[index].name;
    servers.erase(servers.begin() + index);
    Serial.printf("[MCP Client] Removed server: %s\n", name.c_str());
}

void MCPClient::updateServer(int index, const char* name, const char* url, const char* apiKey) {
    if (index < 0 || index >= (int)servers.size()) return;

    servers[index].name = name;
    servers[index].url = url;
    servers[index].apiKey = apiKey ? apiKey : "";
    servers[index].connected = false;  // Need to reconnect

    Serial.printf("[MCP Client] Updated server: %s\n", name);
}

void MCPClient::setServerEnabled(int index, bool enabled) {
    if (index < 0 || index >= (int)servers.size()) return;

    servers[index].enabled = enabled;

    if (!enabled) {
        // Remove tools from disabled server
        for (auto it = tools.begin(); it != tools.end(); ) {
            if (it->serverIndex == index) {
                it = tools.erase(it);
            } else {
                ++it;
            }
        }
        servers[index].connected = false;
    }
}

const MCPServerConfig* MCPClient::getServer(int index) const {
    if (index < 0 || index >= (int)servers.size()) return nullptr;
    return &servers[index];
}

//=============================================================================
// Tool Discovery
//=============================================================================

int MCPClient::discoverTools() {
    tools.clear();
    int totalTools = 0;

    for (int i = 0; i < (int)servers.size(); i++) {
        if (servers[i].enabled) {
            if (discoverServerTools(i)) {
                totalTools += countToolsForServer(i);
            }
        }
    }

    Serial.printf("[MCP Client] Discovered %d tools from %d servers\n",
                  totalTools, servers.size());
    return totalTools;
}

// Helper to count tools for a server
int MCPClient::countToolsForServer(int index) const {
    int count = 0;
    for (const auto& tool : tools) {
        if (tool.serverIndex == index) count++;
    }
    return count;
}

bool MCPClient::discoverServerTools(int index) {
    if (index < 0 || index >= (int)servers.size()) return false;

    MCPServerConfig& server = servers[index];
    if (!server.enabled) return false;

    Serial.printf("[MCP Client] Discovering tools from %s...\n", server.name.c_str());

    // Build tools/list request
    JsonDocument reqDoc;
    reqDoc["jsonrpc"] = "2.0";
    reqDoc["id"] = 1;
    reqDoc["method"] = "tools/list";
    reqDoc["params"] = JsonObject();

    String body;
    serializeJson(reqDoc, body);

    // Make request
    String url = server.url + "/mcp/tools/list";
    String response = makeRequest(url.c_str(), "POST", body.c_str(),
                                   server.apiKey.length() > 0 ? server.apiKey.c_str() : nullptr);

    if (response.length() == 0) {
        server.connected = false;
        server.lastError = "No response from server";
        return false;
    }

    // Parse response
    JsonDocument respDoc;
    DeserializationError error = deserializeJson(respDoc, response);

    if (error) {
        server.connected = false;
        server.lastError = "Invalid JSON response";
        return false;
    }

    // Check for error
    if (respDoc["error"].is<JsonObject>()) {
        server.connected = false;
        server.lastError = respDoc["error"]["message"].as<String>();
        return false;
    }

    // Parse tools
    parseTools(index, response.c_str());
    server.connected = true;
    server.lastError = "";

    return true;
}

void MCPClient::parseTools(int serverIndex, const char* response) {
    JsonDocument doc;
    deserializeJson(doc, response);

    JsonArray toolsArray = doc["result"]["tools"];
    if (!toolsArray) {
        // Try alternative format
        toolsArray = doc["tools"];
    }

    if (!toolsArray) return;

    int toolCount = 0;
    for (JsonObject t : toolsArray) {
        if (tools.size() >= MCP_MAX_SERVERS * MCP_MAX_TOOLS_PER_SERVER) break;

        MCPRemoteTool tool;
        tool.name = t["name"].as<String>();
        tool.description = t["description"].as<String>();

        // Serialize input schema back to string
        serializeJson(t["inputSchema"], tool.inputSchema);

        tool.serverIndex = serverIndex;

        // Prefix tool name with server name to avoid collisions
        tool.name = servers[serverIndex].name + "_" + tool.name;

        tools.push_back(tool);
        toolCount++;
    }

    Serial.printf("[MCP Client] Found %d tools from %s\n",
                  toolCount, servers[serverIndex].name.c_str());
}

const MCPRemoteTool* MCPClient::findTool(const char* name) const {
    for (const auto& tool : tools) {
        if (tool.name == name) {
            return &tool;
        }
    }
    return nullptr;
}

//=============================================================================
// Tool Execution
//=============================================================================

String MCPClient::executeTool(const char* toolName, const char* arguments) {
    const MCPRemoteTool* tool = findTool(toolName);
    if (!tool) {
        return "{\"error\":\"Tool not found\"}";
    }

    if (tool->serverIndex < 0 || tool->serverIndex >= (int)servers.size()) {
        return "{\"error\":\"Invalid server index\"}";
    }

    const MCPServerConfig& server = servers[tool->serverIndex];
    if (!server.enabled || !server.connected) {
        return "{\"error\":\"Server not connected\"}";
    }

    // Extract original tool name (remove server prefix)
    String originalName = tool->name;
    int prefixLen = server.name.length() + 1;  // name + "_"
    if (originalName.length() > prefixLen) {
        originalName = originalName.substring(prefixLen);
    }

    // Build tools/call request
    JsonDocument reqDoc;
    reqDoc["jsonrpc"] = "2.0";
    reqDoc["id"] = millis();  // Use timestamp as ID
    reqDoc["method"] = "tools/call";

    JsonObject params = reqDoc["params"].to<JsonObject>();
    params["name"] = originalName;

    // Parse arguments
    JsonDocument argsDoc;
    deserializeJson(argsDoc, arguments);
    params["arguments"] = argsDoc;

    String body;
    serializeJson(reqDoc, body);

    // Make request
    String url = server.url + "/mcp/tools/call";
    String response = makeRequest(url.c_str(), "POST", body.c_str(),
                                   server.apiKey.length() > 0 ? server.apiKey.c_str() : nullptr);

    Serial.printf("[MCP Client] Executed %s: %s\n", toolName,
                  response.length() > 100 ? (response.substring(0, 100) + "...").c_str() : response.c_str());

    return response;
}

//=============================================================================
// LLM Integration
//=============================================================================

void MCPClient::registerToolsWithLLM(std::function<bool(const char*, const char*, const char*)> addToolFunc) {
    for (const auto& tool : tools) {
        addToolFunc(tool.name.c_str(), tool.description.c_str(), tool.inputSchema.c_str());
    }
    Serial.printf("[MCP Client] Registered %d tools with LLM\n", tools.size());
}

//=============================================================================
// HTTP Request
//=============================================================================

String MCPClient::makeRequest(const char* url, const char* method, const char* body, const char* apiKey) {
    HTTPClient http;
    NetworkClientSecure* secureClient = nullptr;

    // Check if HTTPS
    bool isHttps = String(url).startsWith("https://");
    if (isHttps) {
        secureClient = new NetworkClientSecure();
        secureClient->setInsecure();  // Skip cert verification
        http.begin(*secureClient, url);
    } else {
        http.begin(url);
    }

    http.setTimeout(MCP_HTTP_TIMEOUT);
    http.addHeader("Content-Type", "application/json");

    if (apiKey && strlen(apiKey) > 0) {
        http.addHeader("Authorization", String("Bearer ") + apiKey);
    }

    int httpCode;
    if (strcmp(method, "POST") == 0) {
        httpCode = http.POST(body ? body : "");
    } else {
        httpCode = http.GET();
    }

    String response;
    if (httpCode > 0) {
        response = http.getString();
    } else {
        Serial.printf("[MCP Client] HTTP error: %d\n", httpCode);
    }

    http.end();

    if (secureClient) {
        delete secureClient;
    }

    return response;
}

//=============================================================================
// Persistence
//=============================================================================

void MCPClient::saveConfig() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);

    prefs.putInt("count", servers.size());

    for (int i = 0; i < (int)servers.size(); i++) {
        String prefix = "s" + String(i) + "_";
        prefs.putString((prefix + "name").c_str(), servers[i].name);
        prefs.putString((prefix + "url").c_str(), servers[i].url);
        prefs.putString((prefix + "key").c_str(), servers[i].apiKey);
        prefs.putBool((prefix + "on").c_str(), servers[i].enabled);
    }

    prefs.end();
    Serial.printf("[MCP Client] Saved %d server configs\n", servers.size());
}

void MCPClient::loadConfig() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, true);

    int count = prefs.getInt("count", 0);

    for (int i = 0; i < count && i < MCP_MAX_SERVERS; i++) {
        String prefix = "s" + String(i) + "_";

        MCPServerConfig config;
        config.name = prefs.getString((prefix + "name").c_str(), "");
        config.url = prefs.getString((prefix + "url").c_str(), "");
        config.apiKey = prefs.getString((prefix + "key").c_str(), "");
        config.enabled = prefs.getBool((prefix + "on").c_str(), true);
        config.connected = false;

        if (config.name.length() > 0 && config.url.length() > 0) {
            servers.push_back(config);
        }
    }

    prefs.end();
    Serial.printf("[MCP Client] Loaded %d server configs\n", servers.size());
}

