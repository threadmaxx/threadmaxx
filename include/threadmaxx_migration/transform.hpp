#pragma once

/// @file transform.hpp
/// @brief M4 — `FieldRemap` POD + common transform helpers.
///
/// `FieldRemap` runs a user-supplied `transform(FieldValue&)` on a
/// named field of a matching-typeName Record. Helpers:
/// `widenU16ToU32`, `defaultOnMissing`. Composite operations like
/// "split Transform into position + rotation" are expressible by the
/// caller writing a custom transform that mutates the surrounding
/// `Record` via the wider `FieldRemapWithRecord` form.

#include "records.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace threadmaxx::migration {

/// @brief A per-field transformation.
struct FieldRemap {
    std::string                       typeName;
    std::string                       field;
    std::function<void(FieldValue&)>  transform;
};

/// @brief Apply @p remap to @p rec. Returns `true` if the transform
/// fired (typeName matched and the named field was present);
/// `false` otherwise. Uses the FIRST matching field if there are
/// duplicates.
[[nodiscard]] bool applyRemap(const FieldRemap& remap, Record& rec);

/// @brief Variant that exposes the whole record to the transform —
/// for split / merge operations that need to inspect other fields
/// or push new ones. The transform receives the host Record; the
/// caller decides whether the named field is required.
struct FieldRemapWithRecord {
    std::string                  typeName;
    std::string                  field;
    std::function<void(Record&)> transform;
};

/// @brief Apply @p remap to @p rec. Returns `true` if the transform
/// fired (typeName matched). The transform body decides what to do
/// with the named field; both the `field` slot and the wider record
/// are mutable.
[[nodiscard]] bool applyRemap(const FieldRemapWithRecord& remap, Record& rec);

/// @brief If @p rec lacks a field named @p fieldName, append it with
/// @p defaultBytes. No-op when @p rec.typeName mismatches or the
/// field is already present (regardless of its current value).
/// Returns `true` when the field was inserted.
bool applyDefaultOnMissing(std::string_view typeName,
                           std::string_view fieldName,
                           std::vector<std::byte> defaultBytes,
                           Record& rec);

/// @brief Build a FieldRemap that overwrites the value of a
/// PRESENT-BUT-EMPTY field with the supplied bytes. For the truly
/// missing case, use `applyDefaultOnMissing` instead — FieldRemap
/// only fires when the field is present.
[[nodiscard]] FieldRemap
defaultIfEmpty(std::string typeName,
               std::string field,
               std::vector<std::byte> defaultBytes);

/// @brief Helper: widen a `u16` payload to `u32` (zero-extension).
/// The field's bytes MUST be exactly 2 bytes wide on entry; no-op
/// otherwise (lets you sequence the same step over heterogeneous
/// records without crashing).
[[nodiscard]] FieldRemap widenU16ToU32(std::string typeName,
                                       std::string field);

} // namespace threadmaxx::migration
