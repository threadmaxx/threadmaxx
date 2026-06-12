#pragma once

/// @file version.hpp
/// @brief threadmaxx_reflect version + SemVer ABI contract.
///
/// `THREADMAXX_REFLECT_VERSION` is the integer form
/// `MAJOR * 10000 + MINOR * 100 + PATCH`. `version_string()` returns
/// the human-readable form. Bump all three when releasing.

#include <string_view>

#define THREADMAXX_REFLECT_VERSION_MAJOR 0
#define THREADMAXX_REFLECT_VERSION_MINOR 9
#define THREADMAXX_REFLECT_VERSION_PATCH 0
#define THREADMAXX_REFLECT_VERSION \
    (THREADMAXX_REFLECT_VERSION_MAJOR * 10000 + \
     THREADMAXX_REFLECT_VERSION_MINOR * 100 + \
     THREADMAXX_REFLECT_VERSION_PATCH)

namespace threadmaxx::reflect {

/// @brief Human-readable version string ("0.9.0-dev" during R1-R8, "1.0.0" at close-out).
constexpr std::string_view version_string() noexcept {
    return "0.9.0-dev";
}

} // namespace threadmaxx::reflect
