/// @file StudioVersion.cpp
/// @brief Library anchor TU. Forces a non-empty static archive in ST1
/// before any concrete impl lands.

#include <threadmaxx_studio/version.hpp>

namespace threadmaxx::studio {

// Sanity-pin the version macro at TU-build time. Future ST batches
// will replace this with real anchors.
static_assert(THREADMAXX_STUDIO_VERSION ==
              THREADMAXX_STUDIO_VERSION_MAJOR * 10000 +
              THREADMAXX_STUDIO_VERSION_MINOR * 100 +
              THREADMAXX_STUDIO_VERSION_PATCH,
              "studio version macro is inconsistent");

} // namespace threadmaxx::studio
