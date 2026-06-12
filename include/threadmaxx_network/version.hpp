#pragma once

/// @file version.hpp
/// @brief threadmaxx_network version + SemVer ABI contract.
///
/// `THREADMAXX_NETWORK_VERSION = MAJOR*10000 + MINOR*100 + PATCH`.
/// Bump all three when releasing.

#include <string_view>

#define THREADMAXX_NETWORK_VERSION_MAJOR 0
#define THREADMAXX_NETWORK_VERSION_MINOR 9
#define THREADMAXX_NETWORK_VERSION_PATCH 0
#define THREADMAXX_NETWORK_VERSION \
    (THREADMAXX_NETWORK_VERSION_MAJOR * 10000 + \
     THREADMAXX_NETWORK_VERSION_MINOR * 100 + \
     THREADMAXX_NETWORK_VERSION_PATCH)

namespace threadmaxx::network {

/// @brief Human-readable version. Pre-1.0 development series; flipped
/// to "1.0.0" at the v1.0 close-out batch.
constexpr std::string_view version_string() noexcept {
    return "0.9.0-dev";
}

} // namespace threadmaxx::network
