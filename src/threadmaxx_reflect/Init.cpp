/// @file Init.cpp
/// @brief Placeholder TU so threadmaxx_reflect compiles as a STATIC
/// target from R1 onward. Subsequent batches (R3, R6, R7, R8) drop
/// real sources next to this file.

#include <threadmaxx_reflect/version.hpp>

namespace threadmaxx::reflect {

/// @brief Runtime accessor — single TU owns the symbol so the linker
/// catches duplicate-symbol drift if a header re-defines the version.
int versionInteger() noexcept {
    return THREADMAXX_REFLECT_VERSION;
}

} // namespace threadmaxx::reflect
