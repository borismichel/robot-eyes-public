/**
 * @file llm_client.cpp
 * @brief LLM API client implementation (Claude and OpenAI)
 */

#include "llm_client.h"
#include <NetworkClientSecure.h>

//=============================================================================
// Default System Prompt
//=============================================================================

static const char* DEFAULT_SYSTEM_PROMPT =
    "You are DeskBuddy, a helpful and friendly desk companion robot with expressive eyes. "
    "You have a playful personality and enjoy helping your human friend with tasks. "
    "Keep responses concise and conversational - you're speaking out loud. "
    "You can control your expressions, start timers, and help with productivity. "
    "When appropriate, include an emotion hint in brackets at the start of your response, "
    "like [happy], [curious], [thinking], or [excited].";

//=============================================================================
// Constructor / Destructor
//=============================================================================

LLMClient::LLMClient()
    : initialized(false)
    , provider(LLMProvider::Claude)
    , contextTokens(0)
    , toolExecutor(nullptr)
    , secureClient(nullptr)
{
    memset(apiKey, 0, sizeof(apiKey));
    memset(lastError, 0, sizeof(lastError));
    systemPrompt = DEFAULT_SYSTEM_PROMPT;
}

LLMClient::~LLMClient() {
    end();
}

//=============================================================================
// Initialization
//=============================================================================

bool LLMClient::begin(const char* key, LLMProvider prov) {
    if (initialized) return true;

    if (!key || strlen(key) == 0) {
        Serial.println("[LLM] ERROR: API key required");
        return false;
    }

    setApiKey(key);
    provider = prov;

    // Create secure client
    secureClient = new NetworkClientSecure();
    if (!secureClient) {
        Serial.println("[LLM] ERROR: Failed to create secure client");
        return false;
    }

    // Skip certificate verification
    secureClient->setInsecure();

    initialized = true;
    Serial.printf("[LLM] Initialized with %s\n",
                  provider == LLMProvider::Claude ? "Claude" : "OpenAI");
    return true;
}

void LLMClient::end() {
    if (!initialized) return;

    clearHistory();
    clearTools();

    if (secureClient) {
        delete secureClient;
        secureClient = nullptr;
    }

    initialized = false;
    Serial.println("[LLM] Shutdown");
}

void LLMClient::setApiKey(const char* key) {
    if (key) {
        strncpy(apiKey, key, sizeof(apiKey) - 1);
        apiKey[sizeof(apiKey) - 1] = '\0';
    }
}

void LLMClient::setSystemPrompt(const char* prompt) {
    if (prompt) {
        systemPrompt = prompt;
    }
}

//=============================================================================
// Conversation
//=============================================================================

LLMResponse LLMClient::send(const char* text) {
    LLMResponse response;
    response.success = false;

    if (!initialized) {
        response.error = "Not initialized";
        return response;
    }

    if (!text || strlen(text) == 0) {
        response.error = "Empty message";
        return response;
    }

    Serial.printf("[LLM] User: %s\n", text);

    // Build request based on provider
    String body;
    if (provider == LLMProvider::Claude) {
        body = buildClaudeRequest(text);
    } else {
        body = buildOpenAIRequest(text);
    }

    // Make request
    response = makeRequest(body);

    if (response.success) {
        // Add user message to history
        addMessage(MessageRole::User, text);

        // Add assistant response to history
        if (!response.toolCalls.empty()) {
            for (const auto& tc : response.toolCalls) {
                addMessage(MessageRole::Assistant, response.text.c_str(),
                          tc.id.c_str(), tc.name.c_str(), tc.input.c_str());
            }
        } else {
            addMessage(MessageRole::Assistant, response.text.c_str());
        }

        // Extract emotion hint
        response.emotion = extractEmotion(response.text.c_str());

        Serial.printf("[LLM] Response: %.100s%s\n",
                     response.text.c_str(),
                     response.text.length() > 100 ? "..." : "");

        if (!response.emotion.isEmpty()) {
            Serial.printf("[LLM] Emotion: %s\n", response.emotion.c_str());
        }
    }

    return response;
}

void LLMClient::sendAsync(const char* text, ResponseCallback callback) {
    LLMResponse response = send(text);
    if (callback) {
        callback(response);
    }
}

LLMResponse LLMClient::addToolResult(const char* toolUseId, const char* result) {
    LLMResponse response;
    response.success = false;

    if (!initialized) {
        response.error = "Not initialized";
        return response;
    }

    // Add tool result to history
    addMessage(MessageRole::Tool, result, toolUseId, nullptr, nullptr);

    // Build and send request
    String body;
    if (provider == LLMProvider::Claude) {
        body = buildClaudeRequest(nullptr);
    } else {
        body = buildOpenAIRequest(nullptr);
    }

    response = makeRequest(body);

    if (response.success) {
        addMessage(MessageRole::Assistant, response.text.c_str());
        response.emotion = extractEmotion(response.text.c_str());
    }

    return response;
}

void LLMClient::clearHistory() {
    history.clear();
    contextTokens = 0;
}

//=============================================================================
// Tool Management
//=============================================================================

bool LLMClient::addTool(const char* name, const char* description, const char* inputSchema) {
    if (tools.size() >= LLM_MAX_TOOLS) {
        Serial.println("[LLM] Max tools reached");
        return false;
    }

    for (const auto& t : tools) {
        if (t.name == name) {
            Serial.printf("[LLM] Tool %s already exists\n", name);
            return false;
        }
    }

    ToolDefinition tool;
    tool.name = name;
    tool.description = description;
    tool.inputSchema = inputSchema;

    tools.push_back(tool);
    Serial.printf("[LLM] Added tool: %s\n", name);
    return true;
}

void LLMClient::removeTool(const char* name) {
    for (auto it = tools.begin(); it != tools.end(); ++it) {
        if (it->name == name) {
            tools.erase(it);
            Serial.printf("[LLM] Removed tool: %s\n", name);
            return;
        }
    }
}

void LLMClient::clearTools() {
    tools.clear();
}

//=============================================================================
// Claude Request Building
//=============================================================================

String LLMClient::buildClaudeRequest(const char* newUserMessage) {
    JsonDocument doc;

    doc["model"] = CLAUDE_MODEL;
    doc["max_tokens"] = LLM_MAX_TOKENS;
    doc["system"] = systemPrompt;

    JsonArray messages = doc["messages"].to<JsonArray>();

    // Add history
    for (const auto& msg : history) {
        JsonObject msgObj = messages.add<JsonObject>();

        if (msg.role == MessageRole::User) {
            msgObj["role"] = "user";
            msgObj["content"] = msg.content;
        } else if (msg.role == MessageRole::Tool) {
            msgObj["role"] = "user";
            JsonArray content = msgObj["content"].to<JsonArray>();
            JsonObject toolResult = content.add<JsonObject>();
            toolResult["type"] = "tool_result";
            toolResult["tool_use_id"] = msg.toolUseId;
            toolResult["content"] = msg.content;
        } else {
            msgObj["role"] = "assistant";

            if (!msg.toolName.isEmpty()) {
                JsonArray content = msgObj["content"].to<JsonArray>();
                if (!msg.content.isEmpty()) {
                    JsonObject textBlock = content.add<JsonObject>();
                    textBlock["type"] = "text";
                    textBlock["text"] = msg.content;
                }
                JsonObject toolUse = content.add<JsonObject>();
                toolUse["type"] = "tool_use";
                toolUse["id"] = msg.toolUseId;
                toolUse["name"] = msg.toolName;
                JsonDocument inputDoc;
                deserializeJson(inputDoc, msg.toolInput);
                toolUse["input"] = inputDoc;
            } else {
                msgObj["content"] = msg.content;
            }
        }
    }

    // Add new user message
    if (newUserMessage && strlen(newUserMessage) > 0) {
        JsonObject userMsg = messages.add<JsonObject>();
        userMsg["role"] = "user";
        userMsg["content"] = newUserMessage;
    }

    // Add tools
    if (!tools.empty()) {
        JsonArray toolsArray = doc["tools"].to<JsonArray>();
        for (const auto& tool : tools) {
            JsonObject toolObj = toolsArray.add<JsonObject>();
            toolObj["name"] = tool.name;
            toolObj["description"] = tool.description;
            JsonDocument schemaDoc;
            deserializeJson(schemaDoc, tool.inputSchema);
            toolObj["input_schema"] = schemaDoc;
        }
    }

    String body;
    serializeJson(doc, body);
    return body;
}

//=============================================================================
// OpenAI Request Building
//=============================================================================

String LLMClient::buildOpenAIRequest(const char* newUserMessage) {
    JsonDocument doc;

    doc["model"] = OPENAI_MODEL;
    doc["max_tokens"] = LLM_MAX_TOKENS;

    JsonArray messages = doc["messages"].to<JsonArray>();

    // Add system message
    JsonObject sysMsg = messages.add<JsonObject>();
    sysMsg["role"] = "system";
    sysMsg["content"] = systemPrompt;

    // Add history
    for (const auto& msg : history) {
        JsonObject msgObj = messages.add<JsonObject>();

        if (msg.role == MessageRole::User) {
            msgObj["role"] = "user";
            msgObj["content"] = msg.content;
        } else if (msg.role == MessageRole::Tool) {
            msgObj["role"] = "tool";
            msgObj["tool_call_id"] = msg.toolUseId;
            msgObj["content"] = msg.content;
        } else {
            msgObj["role"] = "assistant";
            if (!msg.toolName.isEmpty()) {
                // For tool calls, content can be null or empty
                if (!msg.content.isEmpty()) {
                    msgObj["content"] = msg.content;
                } else {
                    msgObj["content"] = nullptr;
                }
                JsonArray toolCalls = msgObj["tool_calls"].to<JsonArray>();
                JsonObject tc = toolCalls.add<JsonObject>();
                tc["id"] = msg.toolUseId;
                tc["type"] = "function";
                JsonObject func = tc["function"].to<JsonObject>();
                func["name"] = msg.toolName;
                func["arguments"] = msg.toolInput;
            } else {
                msgObj["content"] = msg.content;
            }
        }
    }

    // Add new user message
    if (newUserMessage && strlen(newUserMessage) > 0) {
        JsonObject userMsg = messages.add<JsonObject>();
        userMsg["role"] = "user";
        userMsg["content"] = newUserMessage;
    }

    // Add tools (OpenAI format)
    if (!tools.empty()) {
        JsonArray toolsArray = doc["tools"].to<JsonArray>();
        for (const auto& tool : tools) {
            JsonObject toolObj = toolsArray.add<JsonObject>();
            toolObj["type"] = "function";
            JsonObject func = toolObj["function"].to<JsonObject>();
            func["name"] = tool.name;
            func["description"] = tool.description;
            JsonDocument schemaDoc;
            deserializeJson(schemaDoc, tool.inputSchema);
            func["parameters"] = schemaDoc;
        }
    }

    String body;
    serializeJson(doc, body);
    return body;
}

//=============================================================================
// Request Execution
//=============================================================================

LLMResponse LLMClient::makeRequest(const String& body) {
    LLMResponse response;
    response.success = false;
    response.inputTokens = 0;
    response.outputTokens = 0;

    String url = "https://";
    if (provider == LLMProvider::Claude) {
        url += CLAUDE_API_HOST;
        url += CLAUDE_API_PATH;
    } else {
        url += OPENAI_API_HOST;
        url += OPENAI_API_PATH;
    }

    http.begin(*secureClient, url);
    http.setTimeout(LLM_HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");

    if (provider == LLMProvider::Claude) {
        http.addHeader("x-api-key", apiKey);
        http.addHeader("anthropic-version", CLAUDE_API_VERSION);
    } else {
        String authHeader = "Bearer ";
        authHeader += apiKey;
        http.addHeader("Authorization", authHeader);
    }

    int httpCode = http.POST(body);

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[LLM] HTTP error: %d\n", httpCode);
        String errBody = http.getString();
        Serial.printf("[LLM] Response: %.200s\n", errBody.c_str());

        snprintf(lastError, sizeof(lastError), "HTTP %d", httpCode);
        response.error = lastError;
        http.end();
        return response;
    }

    String responseBody = http.getString();
    http.end();

    if (provider == LLMProvider::Claude) {
        return parseClaudeResponse(responseBody.c_str());
    } else {
        return parseOpenAIResponse(responseBody.c_str());
    }
}

//=============================================================================
// Claude Response Parsing
//=============================================================================

LLMResponse LLMClient::parseClaudeResponse(const char* json) {
    LLMResponse response;
    response.success = false;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        Serial.printf("[LLM] JSON parse error: %s\n", error.c_str());
        response.error = "JSON parse error";
        return response;
    }

    if (doc["error"].is<JsonObject>()) {
        const char* errMsg = doc["error"]["message"];
        snprintf(lastError, sizeof(lastError), "%s", errMsg ? errMsg : "API error");
        response.error = lastError;
        return response;
    }

    response.inputTokens = doc["usage"]["input_tokens"] | 0;
    response.outputTokens = doc["usage"]["output_tokens"] | 0;
    contextTokens += response.inputTokens + response.outputTokens;

    JsonArray content = doc["content"];
    if (!content) {
        response.error = "No content in response";
        return response;
    }

    for (JsonObject block : content) {
        const char* type = block["type"];

        if (strcmp(type, "text") == 0) {
            const char* text = block["text"];
            if (text) response.text = text;
        } else if (strcmp(type, "tool_use") == 0) {
            ToolCall tc;
            tc.id = block["id"].as<const char*>();
            tc.name = block["name"].as<const char*>();
            serializeJson(block["input"], tc.input);
            response.toolCalls.push_back(tc);
        }
    }

    response.success = true;
    pruneHistory();
    return response;
}

//=============================================================================
// OpenAI Response Parsing
//=============================================================================

LLMResponse LLMClient::parseOpenAIResponse(const char* json) {
    LLMResponse response;
    response.success = false;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json);

    if (error) {
        Serial.printf("[LLM] JSON parse error: %s\n", error.c_str());
        response.error = "JSON parse error";
        return response;
    }

    if (doc["error"].is<JsonObject>()) {
        const char* errMsg = doc["error"]["message"];
        snprintf(lastError, sizeof(lastError), "%s", errMsg ? errMsg : "API error");
        response.error = lastError;
        return response;
    }

    response.inputTokens = doc["usage"]["prompt_tokens"] | 0;
    response.outputTokens = doc["usage"]["completion_tokens"] | 0;
    contextTokens += response.inputTokens + response.outputTokens;

    JsonArray choices = doc["choices"];
    if (!choices || choices.size() == 0) {
        response.error = "No choices in response";
        return response;
    }

    JsonObject choice = choices[0];
    JsonObject message = choice["message"];

    // Get text content
    const char* content = message["content"];
    if (content) {
        response.text = content;
    }

    // Check for tool calls
    JsonArray toolCalls = message["tool_calls"];
    if (toolCalls) {
        for (JsonObject tc : toolCalls) {
            ToolCall call;
            call.id = tc["id"].as<const char*>();
            JsonObject func = tc["function"];
            call.name = func["name"].as<const char*>();
            call.input = func["arguments"].as<const char*>();
            response.toolCalls.push_back(call);
        }
    }

    response.success = true;
    pruneHistory();
    return response;
}

//=============================================================================
// History Management
//=============================================================================

void LLMClient::addMessage(MessageRole role, const char* content,
                           const char* toolUseId, const char* toolName,
                           const char* toolInput) {
    Message msg;
    msg.role = role;
    msg.content = content ? content : "";
    msg.toolUseId = toolUseId ? toolUseId : "";
    msg.toolName = toolName ? toolName : "";
    msg.toolInput = toolInput ? toolInput : "";

    history.push_back(msg);
}

void LLMClient::pruneHistory() {
    while (history.size() > LLM_MAX_HISTORY) {
        history.erase(history.begin());
    }

    if (contextTokens > LLM_MAX_CONTEXT_TOKENS) {
        while (history.size() > 2 && contextTokens > LLM_MAX_CONTEXT_TOKENS / 2) {
            contextTokens -= history[0].content.length() / 4;
            history.erase(history.begin());
        }
    }
}

//=============================================================================
// Emotion Extraction
//=============================================================================

String LLMClient::extractEmotion(const char* text) {
    if (!text) return "";

    if (text[0] != '[') return "";

    const char* end = strchr(text, ']');
    if (!end) return "";

    int len = end - text - 1;
    if (len <= 0 || len > 20) return "";

    String emotion;
    emotion.reserve(len);

    for (int i = 1; i <= len; i++) {
        emotion += text[i];
    }

    return emotion;
}
