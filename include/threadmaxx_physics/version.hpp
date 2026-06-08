// threadmaxx_physics — library version.
//
// Semantic versioning (https://semver.org/). Bump rules:
//
//   MAJOR — breaking public API change (signature of a `*.hpp`
//           entry point, layout of a public POD, removal of an
//           `IPhysicsBackend` virtual or a backend factory).
//   MINOR — additive change (new backend factory, new constraint
//           type appended to `ConstraintType`, new query helper).
//           Existing API stays binary + source compatible.
//   PATCH — bug fix or doc improvement. No API change.
//
// `THREADMAXX_PHYSICS_VERSION` is a packed
// `MAJOR * 10000 + MINOR * 100 + PATCH` integer for `#if`-based
// version checks at compile time. `version_string()` returns the
// dotted form for runtime logging.
//
// Lifecycle policy:
//   - The CHANGELOG (`CHANGELOG.md`) tracks every release with
//     human-readable notes.
//   - The maintainer guide (`MAINTAINER_GUIDE.md`) documents the
//     versioning + ABI policy in detail.
//   - Deprecation cycle: `[[deprecated]]` for one minor release,
//     then removal in the next major.

#pragma once

#define THREADMAXX_PHYSICS_VERSION_MAJOR 1
#define THREADMAXX_PHYSICS_VERSION_MINOR 0
#define THREADMAXX_PHYSICS_VERSION_PATCH 0

#define THREADMAXX_PHYSICS_VERSION \
    (THREADMAXX_PHYSICS_VERSION_MAJOR * 10000 + \
     THREADMAXX_PHYSICS_VERSION_MINOR * 100 + \
     THREADMAXX_PHYSICS_VERSION_PATCH)

namespace threadmaxx::physics {

/// Returns the library version as a dotted string `"M.m.p"`.
/// Useful for logging at startup or in a HUD.
constexpr const char* version_string() noexcept {
    return "1.0.0";
}

} // namespace threadmaxx::physics
