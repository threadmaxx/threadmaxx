#pragma once

/// @file threadmaxx_reflect.hpp
/// @brief Umbrella include for the threadmaxx_reflect sibling library.
///
/// Including this header pulls every v1.0 public surface into scope.
/// For tighter compile times, include the per-feature headers directly:
/// `aggregate.hpp`, `macro.hpp`, `registry.hpp`, `enum.hpp`, etc.

#include "aggregate.hpp"
#include "config.hpp"
#include "types.hpp"
#include "version.hpp"

// R2+ headers will be added by subsequent batches:
// #include "macro.hpp"
// #include "field_info.hpp"
// #include "type_info.hpp"
// #include "registry.hpp"
// #include "enum.hpp"
// #include "attributes.hpp"
// #include "visit.hpp"
// #include "value.hpp"
// #include "patch.hpp"
