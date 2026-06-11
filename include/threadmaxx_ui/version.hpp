// threadmaxx_ui — library version.
//
// Semantic versioning (https://semver.org/). Bump rules:
//
//   MAJOR — breaking public API change (signature of a `*.hpp` entry
//           point, layout of a public POD, removal of a function /
//           class / enum value, change to the draw-list wire format).
//   MINOR — additive change (new widget, new draw-cmd kind appended at
//           the END of the enum, new property-inspector overload, new
//           backend). Existing API stays source + binary compatible.
//   PATCH — bug fix, perf, or doc improvement. No API change.
//
// `THREADMAXX_UI_VERSION` is a packed
// `MAJOR * 10000 + MINOR * 100 + PATCH` integer for `#if`-based
// version checks at compile time. `version_string()` returns the
// dotted form for runtime logging.

#pragma once

#define THREADMAXX_UI_VERSION_MAJOR 1
#define THREADMAXX_UI_VERSION_MINOR 0
#define THREADMAXX_UI_VERSION_PATCH 0

#define THREADMAXX_UI_VERSION \
    (THREADMAXX_UI_VERSION_MAJOR * 10000 + \
     THREADMAXX_UI_VERSION_MINOR * 100 + \
     THREADMAXX_UI_VERSION_PATCH)

namespace threadmaxx::ui {

/// Returns the library version as a dotted string.
constexpr const char* version_string() noexcept {
    return "1.0.0";
}

} // namespace threadmaxx::ui
