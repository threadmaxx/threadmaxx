#pragma once

/// @file json.hpp
/// @brief JSON dump / parse driven by `TypeInfo`.
///
/// The runtime path: `to_json(typeInfo, obj)` walks `FieldInfo` and
/// renders primitive fields by `typeIndex`. The compile-time path
/// `to_json<T>(obj)` walks `for_each_field` and resolves types at
/// compile time.

#include <string>
#include <string_view>

#include "../type_info.hpp"
#include "../types.hpp"

namespace threadmaxx::reflect {

/// @brief Render a reflected object as compact JSON. Supports the
/// arithmetic primitives + `bool`. Nested aggregates and strings will
/// land in v1.x; today the JsonVisitor emits `null` for unknown types.
std::string to_json(const TypeInfo* info, const void* obj);

/// @brief Inverse: parse a compact JSON object back into `obj`. Only
/// primitive fields supported in v1.0; unknown fields are ignored,
/// missing fields keep their existing value.
ReflectResult<void> from_json(const TypeInfo* info,
                              void* obj,
                              std::string_view json);

} // namespace threadmaxx::reflect
