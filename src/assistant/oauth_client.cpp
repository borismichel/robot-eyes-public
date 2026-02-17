/**
 * @file oauth_client.cpp
 * @brief OAuth 2.1 client implementation for MCP server authentication
 */

#include "oauth_client.h"
#include "http_helpers.h"
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <mbedtls/sha256.h>
#include <esp_random.h>

static const char* PREFS_NAMESPACE = "oauth";

//=============================================================================
// Constructor / Destructor
//=============================================================================

OAuthClient::OAuthClient()
    : initialized(false)
{
    for (int i = 0; i < OAUTH_MAX_SERVERS; i++) {
        tokens[i].expiresAt = 0;
    }
}

OAuthClient::~OAuthClient() {
    end();
}

//=============================================================================
// Initialization
//=============================================================================

bool OAuthClient::begin() {
    if (initialized) return true;

    loadTokens();

    initialized = true;
    Serial.println("[OAuth] Initialized");
    return true;
}

void OAuthClient::end() {
    if (!initialized) return;

    saveTokens();
    pendingAuth.clear();

    initialized = false;
    Serial.println("[OAuth] Shutdown");
}

//=============================================================================
// Metadata Discovery
//=============================================================================

bool OAuthClient::discoverMetadata(const char* serverUrl, OAuthMetadata& metadata) {
    // MCP spec: check /.well-known/oauth-authorization-server
    String url = String(serverUrl);
    // Remove trailing slash
    if (url.endsWith("/")) url.remove(url.length() - 1);
    url += "/.well-known/oauth-authorization-server";

    Serial.printf("[OAuth] Discovering metadata from %s\n", url.c_str());

    String response = httpRequest(url.c_str(), "GET");
    if (response.isEmpty()) {
        Serial.println("[OAuth] No response from metadata endpoint");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Serial.printf("[OAuth] Metadata JSON parse error: %s\n", error.c_str());
        return false;
    }

    metadata.authorizationEndpoint = doc["authorization_endpoint"].as<String>();
    metadata.tokenEndpoint = doc["token_endpoint"].as<String>();
    metadata.registrationEndpoint = doc["registration_endpoint"].as<String>();

    if (metadata.authorizationEndpoint.isEmpty() || metadata.tokenEndpoint.isEmpty()) {
        Serial.println("[OAuth] Metadata missing required endpoints");
        return false;
    }

    Serial.printf("[OAuth] Found auth=%s token=%s registration=%s\n",
                  metadata.authorizationEndpoint.c_str(),
                  metadata.tokenEndpoint.c_str(),
                  metadata.registrationEndpoint.isEmpty() ? "(none)" : metadata.registrationEndpoint.c_str());
    return true;
}

//=============================================================================
// Dynamic Client Registration (RFC 7591)
//=============================================================================

bool OAuthClient::registerClient(const char* registrationEndpoint, const char* redirectUri,
                                  String& clientId, String& clientSecret) {
    Serial.printf("[OAuth] Registering client at %s\n", registrationEndpoint);

    JsonDocument doc;
    doc["client_name"] = "DeskBuddy";
    doc["redirect_uris"][0] = redirectUri;
    doc["grant_types"][0] = "authorization_code";
    doc["grant_types"][1] = "refresh_token";
    doc["response_types"][0] = "code";
    doc["token_endpoint_auth_method"] = "none";  // Public client (no secret)

    String body;
    serializeJson(doc, body);

    String response = httpRequest(registrationEndpoint, "POST", body.c_str(), "application/json");
    if (response.isEmpty()) {
        Serial.println("[OAuth] No response from registration endpoint");
        return false;
    }

    JsonDocument respDoc;
    DeserializationError error = deserializeJson(respDoc, response);
    if (error) {
        Serial.printf("[OAuth] Registration JSON parse error: %s\n", error.c_str());
        return false;
    }

    clientId = respDoc["client_id"].as<String>();
    clientSecret = respDoc["client_secret"].as<String>();

    if (clientId.isEmpty()) {
        Serial.println("[OAuth] Registration failed: no client_id in response");
        return false;
    }

    Serial.printf("[OAuth] Registered client_id=%s\n", clientId.c_str());
    return true;
}

//=============================================================================
// Authorization URL
//=============================================================================

String OAuthClient::buildAuthorizationUrl(int serverIndex, const OAuthMetadata& metadata,
                                           const char* clientId, const char* clientSecret,
                                           const char* redirectUri, const char* scopes) {
    if (serverIndex < 0 || serverIndex >= OAUTH_MAX_SERVERS) return "";

    // Generate PKCE
    String codeVerifier = generateCodeVerifier();
    String codeChallenge = generateCodeChallenge(codeVerifier);
    String state = generateState();

    // Store pending auth state (needed for handleCallback)
    pendingAuth.clear();
    pendingAuth.serverIndex = serverIndex;
    pendingAuth.codeVerifier = codeVerifier;
    pendingAuth.state = state;
    pendingAuth.redirectUri = redirectUri;
    pendingAuth.tokenUrl = metadata.tokenEndpoint;
    pendingAuth.clientId = clientId;
    pendingAuth.clientSecret = clientSecret ? clientSecret : "";
    pendingAuth.active = true;

    // Build URL
    String url = metadata.authorizationEndpoint;
    url += "?response_type=code";
    url += "&client_id=" + String(clientId);
    url += "&redirect_uri=" + String(redirectUri);
    url += "&state=" + state;
    url += "&code_challenge=" + codeChallenge;
    url += "&code_challenge_method=S256";
    if (scopes && strlen(scopes) > 0) {
        url += "&scope=" + String(scopes);
    }

    Serial.printf("[OAuth] Authorization URL built for server %d\n", serverIndex);
    return url;
}

//=============================================================================
// Callback Handler
//=============================================================================

bool OAuthClient::handleCallback(const char* code, const char* state) {
    if (!pendingAuth.active) {
        Serial.println("[OAuth] No pending authorization");
        return false;
    }

    // Verify state parameter (CSRF protection)
    if (pendingAuth.state != state) {
        Serial.println("[OAuth] State mismatch — possible CSRF");
        pendingAuth.clear();
        return false;
    }

    Serial.printf("[OAuth] Handling callback for server %d\n", pendingAuth.serverIndex);

    // Exchange code for tokens
    bool success = exchangeCodeForTokens(
        pendingAuth.tokenUrl.c_str(),
        code,
        pendingAuth.codeVerifier.c_str(),
        pendingAuth.clientId.c_str(),
        pendingAuth.clientSecret.c_str(),
        pendingAuth.redirectUri.c_str(),
        pendingAuth.serverIndex
    );

    if (success) {
        // Store client credentials alongside tokens for future refresh
        int idx = pendingAuth.serverIndex;
        tokens[idx].clientId = pendingAuth.clientId;
        tokens[idx].clientSecret = pendingAuth.clientSecret;
        tokens[idx].authUrl = "";  // Could store auth URL for re-auth if needed
        tokens[idx].tokenUrl = pendingAuth.tokenUrl;

        saveTokens();
        Serial.printf("[OAuth] Authorization complete for server %d\n", idx);
    }

    pendingAuth.clear();
    return success;
}

//=============================================================================
// Token Management
//=============================================================================

String OAuthClient::getAccessToken(int serverIndex) {
    if (serverIndex < 0 || serverIndex >= OAUTH_MAX_SERVERS) return "";

    OAuthTokens& t = tokens[serverIndex];

    if (!t.isValid()) return "";

    // Refresh if expired or about to expire
    if (t.needsRefresh()) {
        Serial.printf("[OAuth] Token for server %d needs refresh\n", serverIndex);
        if (!refreshAccessToken(serverIndex)) {
            Serial.printf("[OAuth] Refresh failed for server %d\n", serverIndex);
            return "";
        }
    }

    return t.accessToken;
}

bool OAuthClient::hasValidAuth(int serverIndex) const {
    if (serverIndex < 0 || serverIndex >= OAUTH_MAX_SERVERS) return false;
    const OAuthTokens& t = tokens[serverIndex];
    return t.isValid() && !t.isExpired();
}

const char* OAuthClient::getStatus(int serverIndex) const {
    if (serverIndex < 0 || serverIndex >= OAUTH_MAX_SERVERS) return "none";
    const OAuthTokens& t = tokens[serverIndex];

    if (!t.isValid()) return "none";
    if (t.isExpired()) {
        return t.refreshToken.length() > 0 ? "needs_refresh" : "expired";
    }
    return "authorized";
}

void OAuthClient::clearAuth(int serverIndex) {
    if (serverIndex < 0 || serverIndex >= OAUTH_MAX_SERVERS) return;

    tokens[serverIndex] = OAuthTokens();
    saveTokens();
    Serial.printf("[OAuth] Cleared auth for server %d\n", serverIndex);
}

//=============================================================================
// Token Exchange
//=============================================================================

bool OAuthClient::exchangeCodeForTokens(const char* tokenUrl, const char* code,
                                          const char* codeVerifier, const char* clientId,
                                          const char* clientSecret, const char* redirectUri,
                                          int serverIndex) {
    if (serverIndex < 0 || serverIndex >= OAUTH_MAX_SERVERS) return false;

    // Build form-encoded body
    String body = "grant_type=authorization_code";
    body += "&code=" + String(code);
    body += "&redirect_uri=" + String(redirectUri);
    body += "&client_id=" + String(clientId);
    body += "&code_verifier=" + String(codeVerifier);
    if (clientSecret && strlen(clientSecret) > 0) {
        body += "&client_secret=" + String(clientSecret);
    }

    Serial.printf("[OAuth] Exchanging code at %s\n", tokenUrl);

    String response = httpRequest(tokenUrl, "POST", body.c_str(),
                                   "application/x-www-form-urlencoded");
    if (response.isEmpty()) {
        Serial.println("[OAuth] No response from token endpoint");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Serial.printf("[OAuth] Token JSON parse error: %s\n", error.c_str());
        return false;
    }

    if (doc["error"].is<const char*>()) {
        Serial.printf("[OAuth] Token error: %s — %s\n",
                      doc["error"].as<const char*>(),
                      doc["error_description"].as<const char*>());
        return false;
    }

    OAuthTokens& t = tokens[serverIndex];
    t.accessToken = doc["access_token"].as<String>();
    t.refreshToken = doc["refresh_token"].as<String>();

    int expiresIn = doc["expires_in"] | 3600;  // Default 1 hour
    t.expiresAt = (millis() / 1000) + expiresIn;

    Serial.printf("[OAuth] Got tokens for server %d (expires in %ds)\n", serverIndex, expiresIn);
    return t.accessToken.length() > 0;
}

bool OAuthClient::refreshAccessToken(int serverIndex) {
    if (serverIndex < 0 || serverIndex >= OAUTH_MAX_SERVERS) return false;

    OAuthTokens& t = tokens[serverIndex];
    if (t.refreshToken.isEmpty() || t.tokenUrl.isEmpty()) {
        Serial.printf("[OAuth] Cannot refresh server %d: missing refresh_token or token_url\n", serverIndex);
        return false;
    }

    String body = "grant_type=refresh_token";
    body += "&refresh_token=" + t.refreshToken;
    body += "&client_id=" + t.clientId;
    if (t.clientSecret.length() > 0) {
        body += "&client_secret=" + t.clientSecret;
    }

    Serial.printf("[OAuth] Refreshing token for server %d\n", serverIndex);

    String response = httpRequest(t.tokenUrl.c_str(), "POST", body.c_str(),
                                   "application/x-www-form-urlencoded");
    if (response.isEmpty()) {
        Serial.println("[OAuth] No response from token refresh");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Serial.printf("[OAuth] Refresh JSON parse error: %s\n", error.c_str());
        return false;
    }

    if (doc["error"].is<const char*>()) {
        Serial.printf("[OAuth] Refresh error: %s\n", doc["error"].as<const char*>());
        // Refresh failed — clear tokens so UI shows "needs re-auth"
        t.accessToken = "";
        t.expiresAt = 0;
        saveTokens();
        return false;
    }

    t.accessToken = doc["access_token"].as<String>();
    if (doc["refresh_token"].is<const char*>()) {
        t.refreshToken = doc["refresh_token"].as<String>();  // Rotation
    }

    int expiresIn = doc["expires_in"] | 3600;
    t.expiresAt = (millis() / 1000) + expiresIn;

    saveTokens();
    Serial.printf("[OAuth] Refreshed token for server %d (expires in %ds)\n", serverIndex, expiresIn);
    return t.accessToken.length() > 0;
}

//=============================================================================
// PKCE
//=============================================================================

String OAuthClient::generateCodeVerifier() {
    // RFC 7636: unreserved characters [A-Z] / [a-z] / [0-9] / "-" / "." / "_" / "~"
    static const char charset[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    static const int charsetLen = sizeof(charset) - 1;

    char verifier[PKCE_VERIFIER_LENGTH + 1];
    for (int i = 0; i < PKCE_VERIFIER_LENGTH; i++) {
        verifier[i] = charset[esp_random() % charsetLen];
    }
    verifier[PKCE_VERIFIER_LENGTH] = '\0';

    return String(verifier);
}

String OAuthClient::generateCodeChallenge(const String& codeVerifier) {
    // code_challenge = base64url(SHA256(code_verifier))
    uint8_t hash[32];
    mbedtls_sha256((const uint8_t*)codeVerifier.c_str(), codeVerifier.length(), hash, 0);

    return base64urlEncode(hash, 32);
}

String OAuthClient::generateState() {
    // 32 random hex characters
    char state[33];
    for (int i = 0; i < 32; i++) {
        state[i] = "0123456789abcdef"[esp_random() % 16];
    }
    state[32] = '\0';
    return String(state);
}

String OAuthClient::base64urlEncode(const uint8_t* data, size_t length) {
    static const char b64[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    String result;
    result.reserve((length * 4 / 3) + 4);

    for (size_t i = 0; i < length; i += 3) {
        uint32_t n = ((uint32_t)data[i]) << 16;
        if (i + 1 < length) n |= ((uint32_t)data[i + 1]) << 8;
        if (i + 2 < length) n |= (uint32_t)data[i + 2];

        result += b64[(n >> 18) & 0x3F];
        result += b64[(n >> 12) & 0x3F];
        if (i + 1 < length) result += b64[(n >> 6) & 0x3F];
        if (i + 2 < length) result += b64[n & 0x3F];
    }

    // Convert to URL-safe: + → -, / → _ (no padding)
    result.replace('+', '-');
    result.replace('/', '_');

    return result;
}

//=============================================================================
// HTTP Helper
//=============================================================================

String OAuthClient::httpRequest(const char* url, const char* method,
                                 const char* body, const char* contentType) {
    HTTPClient http;
    NetworkClientSecure* secureClient = nullptr;

    bool isHttps = String(url).startsWith("https://");
    if (isHttps) {
        secureClient = HttpHelpers::createSecureClient();
        if (!secureClient) return "";
        http.begin(*secureClient, url);
    } else {
        http.begin(url);
    }

    http.setTimeout(OAUTH_HTTP_TIMEOUT);
    if (contentType) {
        http.addHeader("Content-Type", contentType);
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
        Serial.printf("[OAuth] HTTP error: %d for %s\n", httpCode, url);
    }

    http.end();
    delete secureClient;

    return response;
}

//=============================================================================
// Persistence (NVS)
//=============================================================================

void OAuthClient::saveTokens() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);

    for (int i = 0; i < OAUTH_MAX_SERVERS; i++) {
        String p = "s" + String(i) + "_";
        const OAuthTokens& t = tokens[i];

        if (t.accessToken.length() > 0 || t.refreshToken.length() > 0) {
            prefs.putString((p + "cid").c_str(), t.clientId);
            prefs.putString((p + "csec").c_str(), t.clientSecret);
            prefs.putString((p + "aurl").c_str(), t.authUrl);
            prefs.putString((p + "turl").c_str(), t.tokenUrl);
            prefs.putString((p + "at").c_str(), t.accessToken);
            prefs.putString((p + "rt").c_str(), t.refreshToken);
            prefs.putUInt((p + "exp").c_str(), t.expiresAt);
        } else {
            // Clear slot
            prefs.remove((p + "cid").c_str());
            prefs.remove((p + "csec").c_str());
            prefs.remove((p + "aurl").c_str());
            prefs.remove((p + "turl").c_str());
            prefs.remove((p + "at").c_str());
            prefs.remove((p + "rt").c_str());
            prefs.remove((p + "exp").c_str());
        }
    }

    prefs.end();
}

void OAuthClient::loadTokens() {
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, true);

    for (int i = 0; i < OAUTH_MAX_SERVERS; i++) {
        String p = "s" + String(i) + "_";
        OAuthTokens& t = tokens[i];

        t.clientId = prefs.getString((p + "cid").c_str(), "");
        t.clientSecret = prefs.getString((p + "csec").c_str(), "");
        t.authUrl = prefs.getString((p + "aurl").c_str(), "");
        t.tokenUrl = prefs.getString((p + "turl").c_str(), "");
        t.accessToken = prefs.getString((p + "at").c_str(), "");
        t.refreshToken = prefs.getString((p + "rt").c_str(), "");
        t.expiresAt = prefs.getUInt((p + "exp").c_str(), 0);
    }

    prefs.end();

    int loaded = 0;
    for (int i = 0; i < OAUTH_MAX_SERVERS; i++) {
        if (tokens[i].isValid()) loaded++;
    }
    if (loaded > 0) {
        Serial.printf("[OAuth] Loaded tokens for %d servers\n", loaded);
    }
}
