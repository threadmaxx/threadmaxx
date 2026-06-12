#pragma once

/// @file patch.hpp
/// @brief Apply a sequence of named-field writes to a reflected
/// object.
///
/// v1.0 scope: flat field names only — `Health.current` is the path
/// for "the field named `current` on a Health object". Nested
/// aggregates and vector indexing are deferred to v1.x; the parser
/// rejects `.` and `[` in field paths with `ErrorCode::Unsupported`.

#include <string>
#include <vector>

#include "type_info.hpp"
#include "types.hpp"
#include "value.hpp"

namespace threadmaxx::reflect {

struct PatchEntry {
    std::string fieldPath;
    Value       newValue;
};

struct Patch {
    std::vector<PatchEntry> entries;
};

/// @brief Apply every entry in `patch` to `obj` (interpreted as a
/// pointer to a `typeInfo`-typed object). Returns the first failure
/// encountered — partial writes prior to the failure are NOT rolled
/// back.
ReflectResult<void> applyPatch(const TypeInfo* typeInfo,
                               void* obj,
                               const Patch& patch);

/// @brief Read a single field by name into a `Value`. Type mismatch
/// failure on unsupported field types (non-primitive).
ReflectResult<Value> readField(const TypeInfo* typeInfo,
                               const void* obj,
                               std::string_view fieldPath);

} // namespace threadmaxx::reflect
