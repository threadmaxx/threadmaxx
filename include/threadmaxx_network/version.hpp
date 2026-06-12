#pragma once

/// @file version.hpp
/// @brief threadmaxx_network version + SemVer ABI contract.
///
/// `THREADMAXX_NETWORK_VERSION = MAJOR*10000 + MINOR*100 + PATCH`.
/// Bump all three when releasing.

#include <string_view>

#define THREADMAXX_NETWORK_VERSION_MAJOR 1
#define THREADMAXX_NETWORK_VERSION_MINOR 0
#define THREADMAXX_NETWORK_VERSION_PATCH 0
#define THREADMAXX_NETWORK_VERSION \
    (THREADMAXX_NETWORK_VERSION_MAJOR * 10000 + \
     THREADMAXX_NETWORK_VERSION_MINOR * 100 + \
     THREADMAXX_NETWORK_VERSION_PATCH)

namespace threadmaxx::network {

/// @brief Human-readable version.
constexpr std::string_view version_string() noexcept {
    return "1.0.0";
}

} // namespace threadmaxx::network
