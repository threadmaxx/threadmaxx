#pragma once

/// @file registry.hpp
/// @brief M3 — `MigrationRegistry`: register type names + their
/// introduction `SchemaVersion`, declare type aliases, register
/// per-type migration steps, and query for path existence.
///
/// `MigrationFn` rewrites a single `Record` in place; the pipeline
/// (M5) walks chains of these on each input record. Cycle-creating
/// aliases are rejected at `aliasType()` time — the alias graph stays
/// a DAG.

#include "records.hpp"
#include "version.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace threadmaxx::migration {

/// @brief A single migration step's body.
using MigrationFn = std::function<void(Record&)>;

class MigrationRegistry {
public:
    /// @brief Register @p typeName as introduced at @p introduced.
    /// Re-registering the same name is idempotent (replaces the
    /// introduction version with the more-recent call).
    void registerType(std::string typeName, SchemaVersion introduced);

    /// @brief Make @p oldName an alias for @p newName. The new name
    /// MUST already be registered (or be another known alias).
    /// Returns `true` on success, `false` if the alias would create
    /// a cycle (e.g. A→B already exists and the caller registers
    /// B→A) or @p newName is not known. Idempotent: re-declaring an
    /// existing alias is a no-op success.
    bool aliasType(std::string oldName, std::string newName);

    /// @brief Resolve @p name through the alias graph to the
    /// canonical type name. Returns @p name unchanged when no alias
    /// chain exists.
    [[nodiscard]] std::string_view resolveAlias(std::string_view name) const noexcept;

    /// @brief Register a migration step rewriting @p typeName from
    /// @p from to @p to. The same (type, from, to) triple can be
    /// overwritten; subsequent registration replaces the body.
    void addMigration(std::string typeName,
                      SchemaVersion from,
                      SchemaVersion to,
                      MigrationFn fn);

    /// @brief True if @p typeName (or an alias of it) is registered.
    [[nodiscard]] bool knowsType(std::string_view typeName) const noexcept;

    /// @brief SchemaVersion at which @p typeName was introduced.
    /// Resolves aliases. Returns `{0,0,0}` for unknown types.
    [[nodiscard]] SchemaVersion introducedAt(std::string_view typeName) const noexcept;

    /// @brief True if there exists a chain of registered migrations
    /// taking @p typeName from @p from to @p to. Resolves aliases on
    /// @p typeName. Trivially true when `from == to`.
    [[nodiscard]] bool hasPath(std::string_view typeName,
                               SchemaVersion from,
                               SchemaVersion to) const noexcept;

    /// @brief Total registered types (excluding aliases).
    [[nodiscard]] std::size_t typeCount() const noexcept {
        return types_.size();
    }

    /// @brief Total registered aliases.
    [[nodiscard]] std::size_t aliasCount() const noexcept {
        return aliases_.size();
    }

    /// @brief Lookup the migration body for an exact (type, from,
    /// to) triple. Returns `nullptr` if not registered. Resolves
    /// aliases on the type name. Used by the pipeline (M5).
    [[nodiscard]] const MigrationFn*
    findMigration(std::string_view typeName,
                  SchemaVersion from,
                  SchemaVersion to) const noexcept;

    /// @brief Enumerate every (from, to) step registered for
    /// @p typeName (resolves aliases). The returned span aliases
    /// internal storage; do not hold across `addMigration` calls.
    struct StepView {
        SchemaVersion from;
        SchemaVersion to;
    };
    [[nodiscard]] std::vector<StepView>
    listSteps(std::string_view typeName) const;

private:
    struct MigrationStep {
        SchemaVersion from;
        SchemaVersion to;
        MigrationFn   fn;
    };

    struct TypeInfo {
        SchemaVersion              introduced{};
        std::vector<MigrationStep> steps;
    };

    // Resolve helper that returns "" when unknown. Internal-only.
    std::string resolveOrEmpty_(std::string_view name) const;

    std::unordered_map<std::string, TypeInfo>     types_;
    std::unordered_map<std::string, std::string>  aliases_;
};

} // namespace threadmaxx::migration
