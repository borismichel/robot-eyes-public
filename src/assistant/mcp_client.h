/**
 * @file mcp_client.h
 * @brief MCP Client for connecting to external MCP servers
 *
 * Allows DeskBuddy to use tools from external MCP servers,
 * enabling integration with home automation, calendars,
 * smart devices, and other services.
 *
 * Features:
 * - Connect to multiple MCP servers
 * - Discover available tools
 * - Execute remote tools via Claude tool calls
 * - Configurable via web UI
 */

#ifndef MCP_CLIENT_H
#define MCP_CLIENT_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <vector>

// Forward declaration
class OAuthClient;

//=============================================================================
// Configuration
//=============================================================================

/** Maximum MCP servers that can be configured */
#define MCP_MAX_SERVERS 8

/** Maximum tools per server */
#define MCP_MAX_TOOLS_PER_SERVER 16

/** HTTP timeout for MCP requests (ms) */
#define MCP_HTTP_TIMEOUT 10000

//=============================================================================
// Server and Tool Structures
//=============================================================================

/**
 * @struct MCPRemoteTool
 * @brief Tool discovered from an MCP server
 */
struct MCPRemoteTool {
    String name;
    String description;
    String inputSchema;
    int serverIndex;  // Which server this tool belongs to
};

/**
 * @enum MCPAuthMode
 * @brief Authentication mode for an MCP server
 */
enum class MCPAuthMode : uint8_t {
    ApiKey = 0,   ///< Static Bearer token (existing behavior)
    OAuth  = 1    ///< OAuth 2.1 Authorization Code + PKCE
};

/**
 * @struct MCPServerConfig
 * @brief Configuration for an MCP server connection
 */
struct MCPServerConfig {
    String name;          ///< Display name for the server
    String url;           ///< Server URL (e.g., http://192.168.1.100:8080)
    String apiKey;        ///< API key for Bearer token auth (when authMode == ApiKey)
    MCPAuthMode authMode; ///< Authentication mode
    String clientId;      ///< OAuth client_id (manual or from Dynamic Registration)
    String clientSecret;  ///< OAuth client_secret (optional, for confidential clients)
    bool enabled;         ///< Whether this server is active
    bool connected;       ///< Whether we successfully connected
    String lastError;     ///< Last error message if any

    MCPServerConfig() : authMode(MCPAuthMode::ApiKey), enabled(true), connected(false) {}
};

//=============================================================================
// MCPClient Class
//=============================================================================

/**
 * @class MCPClient
 * @brief Client for connecting to external MCP servers
 */
class MCPClient {
public:
    MCPClient();
    ~MCPClient();

    /**
     * @brief Initialize the client
     */
    bool begin();

    /**
     * @brief Cleanup
     */
    void end();

    /**
     * @brief Get the OAuth client (for web server endpoints)
     */
    OAuthClient* getOAuthClient() { return oauthClient; }

    /**
     * @brief Get mutable server list (for web server to update auth fields)
     */
    std::vector<MCPServerConfig>& getServers() { return servers; }

    //-------------------------------------------------------------------------
    // Server Management
    //-------------------------------------------------------------------------

    /**
     * @brief Add a server configuration
     * @param name Display name
     * @param url Server URL
     * @param apiKey Optional API key
     * @return Server index, or -1 on failure
     */
    int addServer(const char* name, const char* url, const char* apiKey = nullptr);

    /**
     * @brief Remove a server
     * @param index Server index
     */
    void removeServer(int index);

    /**
     * @brief Update server configuration
     */
    void updateServer(int index, const char* name, const char* url, const char* apiKey);

    /**
     * @brief Enable/disable a server
     */
    void setServerEnabled(int index, bool enabled);

    /**
     * @brief Get server configuration
     */
    const MCPServerConfig* getServer(int index) const;

    /**
     * @brief Get number of configured servers
     */
    int getServerCount() const { return servers.size(); }

    //-------------------------------------------------------------------------
    // Tool Discovery
    //-------------------------------------------------------------------------

    /**
     * @brief Connect to all enabled servers and discover tools
     * @return Number of tools discovered
     */
    int discoverTools();

    /**
     * @brief Connect to a specific server and get its tools
     * @param index Server index
     * @return true if successful
     */
    bool discoverServerTools(int index);

    /**
     * @brief Get all discovered tools
     */
    const std::vector<MCPRemoteTool>& getTools() const { return tools; }

    /**
     * @brief Get tool count
     */
    int getToolCount() const { return tools.size(); }

    /**
     * @brief Find a tool by name
     * @return Pointer to tool, or nullptr if not found
     */
    const MCPRemoteTool* findTool(const char* name) const;

    //-------------------------------------------------------------------------
    // Tool Execution
    //-------------------------------------------------------------------------

    /**
     * @brief Execute a tool on its server
     * @param toolName Name of the tool
     * @param arguments JSON string with arguments
     * @return JSON result string
     */
    String executeTool(const char* toolName, const char* arguments);

    //-------------------------------------------------------------------------
    // LLM Integration
    //-------------------------------------------------------------------------

    /**
     * @brief Register all discovered tools with an LLM client
     * @param addToolFunc Function to add tools (LLMClient::addTool)
     */
    void registerToolsWithLLM(std::function<bool(const char*, const char*, const char*)> addToolFunc);

    //-------------------------------------------------------------------------
    // Persistence
    //-------------------------------------------------------------------------

    /**
     * @brief Save server configurations to preferences
     */
    void saveConfig();

    /**
     * @brief Load server configurations from preferences
     */
    void loadConfig();

private:
    /**
     * @brief Make HTTP request to MCP server
     */
    String makeRequest(const char* url, const char* method, const char* body, const char* apiKey);

    /**
     * @brief Make HTTP request with OAuth support and 401 retry
     */
    String makeRequestWithRetry(const char* url, const char* method, const char* body,
                                 const char* apiKey, int serverIndex);

    /**
     * @brief Parse tools from server response
     */
    void parseTools(int serverIndex, const char* response);

    /**
     * @brief Count tools belonging to a specific server
     */
    int countToolsForServer(int index) const;

    bool initialized;
    OAuthClient* oauthClient;
    std::vector<MCPServerConfig> servers;
    std::vector<MCPRemoteTool> tools;
};

// Global MCP client instance
extern MCPClient mcpClient;

#endif // MCP_CLIENT_H
