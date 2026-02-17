/**
 * @file oauth_client.h
 * @brief OAuth 2.1 client for MCP server authentication
 *
 * Implements the MCP spec OAuth flow: Authorization Code + PKCE,
 * Dynamic Client Registration (RFC 7591), and automatic token refresh.
 * The browser-based authorization redirect runs through DeskBuddy's
 * web UI (Assistant tab → Connected MCP Servers).
 */

#ifndef OAUTH_CLIENT_H
#define OAUTH_CLIENT_H

#include <Arduino.h>

//=============================================================================
// Configuration
//=============================================================================

/** Maximum OAuth-enabled servers (matches MCP_MAX_SERVERS) */
#define OAUTH_MAX_SERVERS 8

/** HTTP timeout for OAuth requests (ms) */
#define OAUTH_HTTP_TIMEOUT 15000

/** PKCE code_verifier length (RFC 7636: 43-128 chars) */
#define PKCE_VERIFIER_LENGTH 64

/** Token refresh buffer — refresh this many seconds before expiry */
#define TOKEN_REFRESH_BUFFER_S 60

//=============================================================================
// OAuth Metadata
//=============================================================================

/**
 * @struct OAuthMetadata
 * @brief Discovered OAuth server metadata (RFC 8414)
 */
struct OAuthMetadata {
    String authorizationEndpoint;
    String tokenEndpoint;
    String registrationEndpoint;  ///< Empty if Dynamic Client Registration not supported
};

//=============================================================================
// OAuth Token Set
//=============================================================================

/**
 * @struct OAuthTokens
 * @brief Stored tokens for a single MCP server
 */
struct OAuthTokens {
    String clientId;
    String clientSecret;
    String authUrl;         ///< Authorization endpoint (for re-auth)
    String tokenUrl;        ///< Token endpoint
    String accessToken;
    String refreshToken;
    uint32_t expiresAt;     ///< Unix timestamp when access_token expires

    bool isValid() const {
        return accessToken.length() > 0 && expiresAt > 0;
    }

    bool isExpired() const {
        // Use millis-based time since ESP32 may not have NTP synced
        // expiresAt is stored as seconds since boot (not unix time)
        return (millis() / 1000) >= expiresAt;
    }

    bool needsRefresh() const {
        return isValid() && ((millis() / 1000) + TOKEN_REFRESH_BUFFER_S) >= expiresAt;
    }
};

//=============================================================================
// Pending Authorization State (ephemeral, RAM only)
//=============================================================================

struct PendingAuth {
    int serverIndex;
    String codeVerifier;
    String state;
    String redirectUri;
    String tokenUrl;
    String clientId;
    String clientSecret;
    bool active;

    PendingAuth() : serverIndex(-1), active(false) {}
    void clear() {
        serverIndex = -1;
        codeVerifier = "";
        state = "";
        redirectUri = "";
        tokenUrl = "";
        clientId = "";
        clientSecret = "";
        active = false;
    }
};

//=============================================================================
// OAuthClient Class
//=============================================================================

class OAuthClient {
public:
    OAuthClient();
    ~OAuthClient();

    /**
     * @brief Initialize — loads saved tokens from NVS
     */
    bool begin();

    /**
     * @brief Shutdown — saves tokens to NVS
     */
    void end();

    //-------------------------------------------------------------------------
    // Authorization Flow
    //-------------------------------------------------------------------------

    /**
     * @brief Discover OAuth metadata from MCP server
     * @param serverUrl Base URL of the MCP server
     * @param metadata Output: discovered endpoints
     * @return true if metadata found
     */
    bool discoverMetadata(const char* serverUrl, OAuthMetadata& metadata);

    /**
     * @brief Register client dynamically (RFC 7591)
     * @param registrationEndpoint The registration URL from metadata
     * @param redirectUri Callback URL (e.g., http://<buddy-ip>/oauth/callback)
     * @param clientId Output: assigned client_id
     * @param clientSecret Output: assigned client_secret (may be empty)
     * @return true if registration successful
     */
    bool registerClient(const char* registrationEndpoint, const char* redirectUri,
                        String& clientId, String& clientSecret);

    /**
     * @brief Build the full authorization URL for browser redirect
     *
     * Generates PKCE code_verifier/challenge, state parameter, and stores
     * them in pendingAuth for later use in handleCallback().
     *
     * @param serverIndex Index into MCP server list
     * @param metadata Discovered OAuth metadata
     * @param clientId OAuth client ID
     * @param clientSecret OAuth client secret (optional)
     * @param redirectUri Callback URL
     * @param scopes Space-separated scopes (optional)
     * @return Full authorization URL to redirect browser to
     */
    String buildAuthorizationUrl(int serverIndex, const OAuthMetadata& metadata,
                                 const char* clientId, const char* clientSecret,
                                 const char* redirectUri, const char* scopes = nullptr);

    /**
     * @brief Handle the OAuth callback after user authorization
     *
     * Verifies state parameter, exchanges authorization code for tokens,
     * and stores tokens in NVS.
     *
     * @param code Authorization code from callback
     * @param state State parameter from callback
     * @return true if tokens obtained successfully
     */
    bool handleCallback(const char* code, const char* state);

    //-------------------------------------------------------------------------
    // Token Management
    //-------------------------------------------------------------------------

    /**
     * @brief Get a valid access token for a server (refreshes if needed)
     * @param serverIndex Index into MCP server list
     * @return Access token string, or empty string if not authorized
     */
    String getAccessToken(int serverIndex);

    /**
     * @brief Check if a server has valid OAuth authorization
     */
    bool hasValidAuth(int serverIndex) const;

    /**
     * @brief Get OAuth status string for display
     * @return "authorized", "needs_auth", "expired", or "none"
     */
    const char* getStatus(int serverIndex) const;

    /**
     * @brief Clear all OAuth data for a server
     */
    void clearAuth(int serverIndex);

    //-------------------------------------------------------------------------
    // Persistence
    //-------------------------------------------------------------------------

    void saveTokens();
    void loadTokens();

private:
    //-------------------------------------------------------------------------
    // PKCE Helpers
    //-------------------------------------------------------------------------

    /**
     * @brief Generate a random code_verifier string
     */
    String generateCodeVerifier();

    /**
     * @brief Compute code_challenge = base64url(SHA256(code_verifier))
     */
    String generateCodeChallenge(const String& codeVerifier);

    /**
     * @brief Generate a random state parameter (32 hex chars)
     */
    String generateState();

    /**
     * @brief Base64url encode (no padding, URL-safe alphabet)
     */
    static String base64urlEncode(const uint8_t* data, size_t length);

    //-------------------------------------------------------------------------
    // Token Exchange
    //-------------------------------------------------------------------------

    /**
     * @brief Exchange authorization code for tokens
     */
    bool exchangeCodeForTokens(const char* tokenUrl, const char* code,
                                const char* codeVerifier, const char* clientId,
                                const char* clientSecret, const char* redirectUri,
                                int serverIndex);

    /**
     * @brief Refresh an expired access token
     */
    bool refreshAccessToken(int serverIndex);

    //-------------------------------------------------------------------------
    // HTTP Helper
    //-------------------------------------------------------------------------

    /**
     * @brief Make an HTTP request (GET or POST with form/json body)
     */
    String httpRequest(const char* url, const char* method,
                       const char* body = nullptr,
                       const char* contentType = "application/json");

    bool initialized;
    OAuthTokens tokens[OAUTH_MAX_SERVERS];
    PendingAuth pendingAuth;
};

#endif // OAUTH_CLIENT_H
