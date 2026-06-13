#pragma once

/// @file validation.hpp
/// @brief M8 — Validation diagnostics over a RecordSet.
/// `validate()` walks the records and reports unknown types,
/// dangling alias chains, and other consistency violations the
/// pipeline would otherwise have to discover at apply time.

#include "registry.hpp"
#include "savefile.hpp"

#include <string>
#include <vector>

namespace threadmaxx::migration {

struct ValidationReport {
    bool                     ok{false};
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

/// @brief Validate @p set against the registry. Reports unknown
/// types as warnings, missing migration paths to @p target as
/// warnings (no path means the pipeline would error later — but at
/// validate time we treat it as a warning since the host may want to
/// repair the registry).
/// `ok` is `true` iff zero errors. Warnings do not fail.
[[nodiscard]] ValidationReport
validate(const RecordSet& set,
         const MigrationRegistry& registry,
         SchemaVersion target);

/// @brief Lighter-weight variant: report only the unique unknown
/// type names (one warning per distinct type). Useful for the studio
/// MigrationValidatorPanel which renders the list once per save.
[[nodiscard]] std::vector<std::string>
collectUnknownTypes(const RecordSet& set,
                    const MigrationRegistry& registry);

/// @brief Detect alias chains that resolve into a registered type;
/// the result lists alias names whose chain ends in an unknown
/// canonical (or aliases that chain through a cycle, though
/// MigrationRegistry refuses cycles at registration time).
[[nodiscard]] std::vector<std::string>
findOrphanedAliases(const MigrationRegistry& registry,
                    const std::vector<std::string>& aliasNames);

} // namespace threadmaxx::migration
