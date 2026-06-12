/// @file Init.cpp
/// @brief Placeholder TU so the static library has at least one
/// source. Replaced by real sources from NW3 onward.

#include "threadmaxx_network/version.hpp"

namespace threadmaxx::network::internal {

// Forces the version constants into a real linkable symbol so a
// downstream `--whole-archive` link can't strip them entirely.
unsigned versionInteger() noexcept {
    return static_cast<unsigned>(THREADMAXX_NETWORK_VERSION);
}

} // namespace threadmaxx::network::internal
