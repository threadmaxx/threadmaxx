#pragma once

/// @file rename.hpp
/// @brief M4 — `FieldRename` POD + the free function that applies it
/// to a Record in place. The renamer is type-scoped — applying to a
/// Record whose typeName doesn't match @p typeName is a no-op so
/// hosts can pre-build rename specs that target one type without
/// accidentally rewriting unrelated records.

#include "records.hpp"

#include <string>
#include <string_view>

namespace threadmaxx::migration {

struct FieldRename {
    std::string typeName;
    std::string from;
    std::string to;
};

/// @brief Apply @p rename to @p rec in place. Returns the number of
/// fields renamed (0 when typeName mismatches, the source field is
/// missing, or `from == to`). When a record contains multiple
/// fields with the same name (rare but legal) all matches are
/// renamed.
[[nodiscard]] std::size_t
applyRename(const FieldRename& rename, Record& rec);

/// @brief Convenience: build + apply in one call.
[[nodiscard]] inline std::size_t
applyRename(std::string_view typeName,
            std::string_view from,
            std::string_view to,
            Record& rec) {
    FieldRename r{};
    r.typeName = std::string{typeName};
    r.from     = std::string{from};
    r.to       = std::string{to};
    return applyRename(r, rec);
}

} // namespace threadmaxx::migration
