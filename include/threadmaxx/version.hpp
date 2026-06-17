// threadmaxx — library version.
//
// Semantic versioning (https://semver.org/). Bump rules:
//
//   MAJOR — breaking public API change (signature changes on a
//           `Engine`/`World`/`CommandBuffer`/`ISystem` method,
//           removal of a public header or class, layout changes to
//           an exposed POD, change to the on-disk
//           `WorldSnapshot` magic / version).
//   MINOR — additive change (new method on a public class, new
//           public header, new component slot, new event POD, new
//           opt-in feature flag). Existing API stays source- and
//           binary-compatible.
//   PATCH — bug fix or doc improvement with no public-API change.
//
// `THREADMAXX_VERSION` is a packed
// `MAJOR * 10000 + MINOR * 100 + PATCH` integer for `#if`-based
// version checks at compile time. `version_string()` returns the
// dotted form for runtime logging or HUD overlays.
//
// Coordinated state — when bumping:
//   - This file's macros AND the string literal in `version_string()`.
//   - `CMakeLists.txt`'s `project(... VERSION X.Y.Z ...)`.
//   - Append a section to `CHANGELOG.md`.
//
// Lifecycle policy:
//   - The CHANGELOG (`CHANGELOG.md`) tracks every release with
//     human-readable notes.
//   - Deprecation cycle: `[[deprecated]]` for one minor release,
//     then removal in the next major.
//   - The sibling `threadmaxx_simd` library has its own
//     independent semver (see `include/threadmaxx_simd/version.hpp`).

#pragma once

#define THREADMAXX_VERSION_MAJOR 1
#define THREADMAXX_VERSION_MINOR 3
#define THREADMAXX_VERSION_PATCH 0

#define THREADMAXX_VERSION \
    (THREADMAXX_VERSION_MAJOR * 10000 + \
     THREADMAXX_VERSION_MINOR * 100 + \
     THREADMAXX_VERSION_PATCH)

namespace threadmaxx {

/// Returns the library version as a dotted string `"M.m.p"`.
/// Useful for logging at startup or in a HUD overlay.
constexpr const char* version_string() noexcept {
    return "1.3.0";
}

} // namespace threadmaxx
