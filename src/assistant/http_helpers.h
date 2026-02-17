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

} // namespace HttpHelpers

#endif // HTTP_HELPERS_H
