#pragma once

/// @file version.hpp
/// @brief threadmaxx_studio version + SemVer ABI contract.
///
/// `THREADMAXX_STUDIO_VERSION` is the integer form
/// `MAJOR * 10000 + MINOR * 100 + PATCH`. `version_string()` returns
/// the human-readable form. Stamped at 1.0.0 by the M9 v1.0 close-out
/// (batch ST41) on 2026-06-13.

#include <string_view>

#define THREADMAXX_STUDIO_VERSION_MAJOR 1
#define THREADMAXX_STUDIO_VERSION_MINOR 0
#define THREADMAXX_STUDIO_VERSION_PATCH 0
#define THREADMAXX_STUDIO_VERSION \
    (THREADMAXX_STUDIO_VERSION_MAJOR * 10000 + \
     THREADMAXX_STUDIO_VERSION_MINOR * 100 + \
     THREADMAXX_STUDIO_VERSION_PATCH)

namespace threadmaxx::studio {

/// @brief Human-readable version string.
constexpr std::string_view version_string() noexcept {
    return "1.0.0";
}

} // namespace threadmaxx::studio
