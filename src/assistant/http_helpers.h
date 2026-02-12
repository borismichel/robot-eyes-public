/**
 * @file http_helpers.h
 * @brief Shared HTTP client utilities
 */

#ifndef HTTP_HELPERS_H
#define HTTP_HELPERS_H

#include <NetworkClientSecure.h>

namespace HttpHelpers {

inline NetworkClientSecure* createSecureClient() {
    auto* c = new NetworkClientSecure();
    if (c) c->setInsecure();
    return c;
}

/// Recreate a secure client to clear any poisoned connection state.
/// Deletes the old client and returns a fresh one.
inline void resetSecureClient(NetworkClientSecure*& client) {
    if (client) {
        client->stop();
        delete client;
    }
    client = createSecureClient();
}

} // namespace HttpHelpers

#endif // HTTP_HELPERS_H
