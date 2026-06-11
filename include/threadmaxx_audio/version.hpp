// threadmaxx_audio — library version.
//
// Semantic versioning (https://semver.org/). Bump rules:
//
//   MAJOR — breaking public API change (signature of a `*.hpp`
//           entry point, layout of a public POD, removal of a
//           function / class / enum value, change to the wire
//           format magic).
//   MINOR — additive change (new mixer method, new DSP helper, new
//           channel layout value, new backend, additive event
//           type). Existing API stays source + binary compatible.
//   PATCH — bug fix, perf, or doc improvement. No API change.
//
// `THREADMAXX_AUDIO_VERSION` is a packed
// `MAJOR * 10000 + MINOR * 100 + PATCH` integer for `#if`-based
// version checks at compile time. `version_string()` returns the
// dotted form for runtime logging.
//
// Lifecycle policy:
//   - `MAINTAINER_GUIDE.md` documents the versioning + ABI policy
//     in detail.
//   - Deprecation cycle: `[[deprecated]]` for one minor release,
//     then removal in the next major.

#pragma once

#define THREADMAXX_AUDIO_VERSION_MAJOR 1
#define THREADMAXX_AUDIO_VERSION_MINOR 0
#define THREADMAXX_AUDIO_VERSION_PATCH 0

#define THREADMAXX_AUDIO_VERSION \
    (THREADMAXX_AUDIO_VERSION_MAJOR * 10000 + \
     THREADMAXX_AUDIO_VERSION_MINOR * 100 + \
     THREADMAXX_AUDIO_VERSION_PATCH)

namespace threadmaxx::audio {

/// Returns the library version as a dotted string `"M.m.p"`.
/// Useful for logging at startup or in a HUD.
constexpr const char* version_string() noexcept {
    return "1.0.0";
}

} // namespace threadmaxx::audio
