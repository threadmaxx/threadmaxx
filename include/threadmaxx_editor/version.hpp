#pragma once

/// @file version.hpp
/// @brief threadmaxx_editor version + SemVer ABI contract.
///
/// `THREADMAXX_EDITOR_VERSION` is the integer form
/// `MAJOR * 10000 + MINOR * 100 + PATCH`. `version_string()` returns
/// the human-readable form. Bump all three when releasing.

#include <string_view>

#define THREADMAXX_EDITOR_VERSION_MAJOR 0
#define THREADMAXX_EDITOR_VERSION_MINOR 9
#define THREADMAXX_EDITOR_VERSION_PATCH 0
#define THREADMAXX_EDITOR_VERSION \
    (THREADMAXX_EDITOR_VERSION_MAJOR * 10000 + \
     THREADMAXX_EDITOR_VERSION_MINOR * 100 + \
     THREADMAXX_EDITOR_VERSION_PATCH)

namespace threadmaxx::editor {

/// @brief Human-readable version string. Pre-1.0 development series;
/// flipped to "1.0.0" at the v1.0 close-out batch.
constexpr std::string_view version_string() noexcept {
    return "0.9.0-dev";
}

} // namespace threadmaxx::editor
