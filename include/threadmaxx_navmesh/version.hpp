// threadmaxx_navmesh — library version.
//
// Semantic versioning (https://semver.org/). Bump rules:
//
//   MAJOR — breaking public API change (signature of a `*.hpp`
//           entry point, layout of a public POD, removal of a
//           function / class / enum value, change to the wire
//           format magic).
//   MINOR — additive change (new query / steering helper, new
//           bake validation arm, new public method on the registry
//           or services, additive wire-format field gated on a
//           version bump). Existing API stays binary + source
//           compatible.
//   PATCH — bug fix or doc improvement. No API change.
//
// `THREADMAXX_NAVMESH_VERSION` is a packed
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

#define THREADMAXX_NAVMESH_VERSION_MAJOR 1
#define THREADMAXX_NAVMESH_VERSION_MINOR 0
#define THREADMAXX_NAVMESH_VERSION_PATCH 0

#define THREADMAXX_NAVMESH_VERSION \
    (THREADMAXX_NAVMESH_VERSION_MAJOR * 10000 + \
     THREADMAXX_NAVMESH_VERSION_MINOR * 100 + \
     THREADMAXX_NAVMESH_VERSION_PATCH)

namespace threadmaxx::navmesh {

/// Returns the library version as a dotted string `"M.m.p"`.
/// Useful for logging at startup or in a HUD.
constexpr const char* version_string() noexcept {
    return "1.0.0";
}

} // namespace threadmaxx::navmesh
