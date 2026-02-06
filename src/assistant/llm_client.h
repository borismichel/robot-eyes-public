/**
 * @file llm_client.h
 * @brief LLM API client with tool use support (Claude and OpenAI)
 *
 * Sends messages to Claude or OpenAI API and handles responses including
 * tool calls for device control and external MCP tools.
 */

#ifndef LLM_CLIENT_H
#define LLM_CLIENT_H

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <NetworkClientSecure.h>
#include <functional>
#include <vector>

//=============================================================================
// Configuration
//=============================================================================

/** Claude API endpoint */
#define CLAUDE_API_HOST "api.anthropic.com"
#define CLAUDE_API_PATH "/v1/messages"
#define CLAUDE_API_VERSION "2023-06-01"
#define CLAUDE_MODEL "claude-sonnet-4-20250514"

/** OpenAI API endpoint */
#define OPENAI_API_HOST "api.openai.com"
#define OPENAI_API_PATH "/v1/chat/completions"
#define OPENAI_MODEL "gpt-4o"

/** Maximum tokens in response */
#define LLM_MAX_TOKENS 1024

/** Maximum context tokens to maintain */
#define LLM_MAX_CONTEXT_TOKENS 8000

/** HTTP timeout (ms) */
#define LLM_HTTP_TIMEOUT_MS 60000

/** Maximum message history */
#define LLM_MAX_HISTORY 20

/** Maximum tool definitions */
#define LLM_MAX_TOOLS 16

//=============================================================================
// Provider Enum
//=============================================================================

/**
 * @enum LLMProvider
 * @brief Supported LLM providers
 */
enum class LLMProvider {
    Claude,
    OpenAI
};

//=============================================================================
// Message Types
//=============================================================================

/**
 * @enum MessageRole
 * @brief Role of a conversation message
 */
enum class MessageRole {
    User,
    Assistant,
    Tool        // For tool results
};

/**
 * @struct Message
 * @brief A conversation message
 */
struct Message {
    MessageRole role;
    String content;
    String toolUseId;       // For tool results
    String toolName;        // For tool use
    String toolInput;       // For tool use (JSON)
};

/**
 * @struct ToolDefinition
 * @brief Definition of an available tool
 */
struct ToolDefinition {
    String name;
    String description;
    String inputSchema;     // JSON schema
};

/**
 * @struct ToolCall
 * @brief A tool call from LLM
 */
struct ToolCall {
    String id;
    String name;
    String input;           // JSON
};

/**
 * @struct LLMResponse
 * @brief Response from LLM API
 */
struct LLMResponse {
    bool success;
    String text;
    String emotion;         // Extracted emotion hint
    std::vector<ToolCall> toolCalls;
    String error;
    int inputTokens;
    int outputTokens;
};

//=============================================================================
// Callbacks
//=============================================================================

/**
 * @brief Callback for tool execution
 * @param toolName Name of tool to execute
 * @param input Tool input (JSON)
 * @return Tool result (JSON)
 */
using ToolExecutor = std::function<String(const char* toolName, const char* input)>;

/**
 * @brief Callback for response ready
 * @param response The LLM response
 */
using ResponseCallback = std::function<void(const LLMResponse& response)>;

//=============================================================================
// LLMClient Class
//=============================================================================

/**
 * @class LLMClient
 * @brief LLM API client with conversation and tool support
 *
 * Supports both Claude (Anthropic) and OpenAI APIs with a unified interface.
 */
class LLMClient {
public:
    LLMClient();
    ~LLMClient();

    /**
     * @brief Initialize the client
     * @param apiKey API key for the selected provider
     * @param provider LLM provider (Claude or OpenAI)
     * @return true if initialization successful
     */
    bool begin(const char* apiKey, LLMProvider provider = LLMProvider::Claude);

    /**
     * @brief Cleanup
     */
    void end();

    //-------------------------------------------------------------------------
    // Conversation
    //-------------------------------------------------------------------------

    /**
     * @brief Send a message and get response
     * @param text User message text
     * @return LLM response
     */
    LLMResponse send(const char* text);

    /**
     * @brief Send a message asynchronously
     * @param text User message text
     * @param callback Response callback
     */
    void sendAsync(const char* text, ResponseCallback callback);

    /**
     * @brief Add tool result and continue conversation
     * @param toolUseId Tool use ID from LLM
     * @param result Tool execution result (JSON)
     * @return LLM response after tool result
     */
    LLMResponse addToolResult(const char* toolUseId, const char* result);

    /**
     * @brief Clear conversation history
     */
    void clearHistory();

    /**
     * @brief Get conversation token count estimate
     */
    int getContextTokens() const { return contextTokens; }

    //-------------------------------------------------------------------------
    // Configuration
    //-------------------------------------------------------------------------

    /**
     * @brief Set system prompt
     */
    void setSystemPrompt(const char* prompt);

    /**
     * @brief Get system prompt
     */
    const char* getSystemPrompt() const { return systemPrompt.c_str(); }

    /**
     * @brief Set API key
     */
    void setApiKey(const char* key);

    /**
     * @brief Set LLM provider
     */
    void setProvider(LLMProvider prov) { provider = prov; }

    /**
     * @brief Get current provider
     */
    LLMProvider getProvider() const { return provider; }

    /**
     * @brief Set tool executor callback
     */
    void setToolExecutor(ToolExecutor executor) { toolExecutor = executor; }

    //-------------------------------------------------------------------------
    // Tool Management
    //-------------------------------------------------------------------------

    /**
     * @brief Add a tool definition
     * @param name Tool name
     * @param description Tool description
     * @param inputSchema JSON schema for input
     * @return true if added
     */
    bool addTool(const char* name, const char* description, const char* inputSchema);

    /**
     * @brief Remove a tool
     * @param name Tool name
     */
    void removeTool(const char* name);

    /**
     * @brief Clear all tools
     */
    void clearTools();

    /**
     * @brief Get tool count
     */
    int getToolCount() const { return tools.size(); }

    //-------------------------------------------------------------------------
    // State
    //-------------------------------------------------------------------------

    /**
     * @brief Check if initialized
     */
    bool isInitialized() const { return initialized; }

    /**
     * @brief Get last error
     */
    const char* getError() const { return lastError; }

private:
    /**
     * @brief Build request JSON for Claude API
     */
    String buildClaudeRequest(const char* newUserMessage);

    /**
     * @brief Build request JSON for OpenAI API
     */
    String buildOpenAIRequest(const char* newUserMessage);

    /**
     * @brief Parse Claude response JSON
     */
    LLMResponse parseClaudeResponse(const char* json);

    /**
     * @brief Parse OpenAI response JSON
     */
    LLMResponse parseOpenAIResponse(const char* json);

    /**
     * @brief Make API request
     */
    LLMResponse makeRequest(const String& body);

    /**
     * @brief Add message to history
     */
    void addMessage(MessageRole role, const char* content,
                    const char* toolUseId = nullptr,
                    const char* toolName = nullptr,
                    const char* toolInput = nullptr);

    /**
     * @brief Prune history if too long
     */
    void pruneHistory();

    /**
     * @brief Extract emotion from response text
     */
    String extractEmotion(const char* text);

    bool initialized;
    LLMProvider provider;
    char apiKey[128];
    String systemPrompt;

    // Conversation history
    std::vector<Message> history;
    int contextTokens;
    char lastError[256];

    // Tools
    std::vector<ToolDefinition> tools;
    ToolExecutor toolExecutor;

    // HTTP client
    NetworkClientSecure* secureClient;
    HTTPClient http;
};

#endif // LLM_CLIENT_H
