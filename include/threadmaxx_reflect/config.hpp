#pragma once

/// @file config.hpp
/// @brief Compile-time configuration constants for threadmaxx_reflect.

#include <cstddef>

namespace threadmaxx::reflect {

/// @brief Maximum field count detectable by aggregate reflection.
/// Bumping this raises compile time of the field-count probe; v1.0
/// keeps it at 32 (well above every threadmaxx component).
inline constexpr std::size_t kMaxAggregateFields = 32;

/// @brief Default lower / upper bounds for enum reflection scans.
/// Per-enum overrides via `EnumRange<E>` specialization.
inline constexpr int kEnumScanRangeMin = -128;
inline constexpr int kEnumScanRangeMax = 128;

/// @brief Maximum number of attributes per field (compile-time gate).
inline constexpr std::size_t kMaxAttributesPerField = 8;

} // namespace threadmaxx::reflect
