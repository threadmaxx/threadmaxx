#pragma once

/// @file threadmaxx_reflect.hpp
/// @brief Umbrella include for the threadmaxx_reflect sibling library.
///
/// Including this header pulls every v1.0 public surface into scope.
/// For tighter compile times, include the per-feature headers directly:
/// `aggregate.hpp`, `macro.hpp`, `registry.hpp`, `enum.hpp`, etc.

#include "aggregate.hpp"
#include "config.hpp"
#include "field_info.hpp"
#include "macro.hpp"
#include "types.hpp"
#include "version.hpp"

// R3+ headers will be added by subsequent batches:
// #include "type_info.hpp"
// #include "registry.hpp"
// #include "enum.hpp"
// #include "attributes.hpp"
// #include "visit.hpp"
// #include "value.hpp"
// #include "patch.hpp"
