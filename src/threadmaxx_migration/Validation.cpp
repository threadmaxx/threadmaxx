/// @file Validation.cpp
/// @brief M8 — validate() + collectUnknownTypes + findOrphanedAliases.

#include <threadmaxx_migration/validation.hpp>

#include <unordered_set>

namespace threadmaxx::migration {

ValidationReport validate(const RecordSet& set,
                          const MigrationRegistry& registry,
                          SchemaVersion target) {
    ValidationReport report{};
    std::unordered_set<std::string> reportedUnknowns;
    for (const auto& rec : set.records) {
        if (!registry.knowsType(rec.typeName)) {
            if (reportedUnknowns.insert(rec.typeName).second) {
                report.warnings.push_back(
                    "unknown type: " + rec.typeName);
            }
            continue;
        }
        if (rec.sourceVersion != target &&
            !registry.hasPath(rec.typeName, rec.sourceVersion, target)) {
            report.warnings.push_back(
                "no migration path: " + rec.typeName);
        }
    }
    report.ok = report.errors.empty();
    return report;
}

std::vector<std::string>
collectUnknownTypes(const RecordSet& set,
                    const MigrationRegistry& registry) {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const auto& rec : set.records) {
        if (!registry.knowsType(rec.typeName)) {
            if (seen.insert(rec.typeName).second) {
                out.push_back(rec.typeName);
            }
        }
    }
    return out;
}

std::vector<std::string>
findOrphanedAliases(const MigrationRegistry& registry,
                    const std::vector<std::string>& aliasNames) {
    std::vector<std::string> orphans;
    for (const auto& name : aliasNames) {
        // resolveAlias returns the input verbatim when no chain
        // exists — when it returns the input AND that input isn't a
        // registered type, the alias is orphaned.
        if (!registry.knowsType(name)) {
            orphans.push_back(name);
        }
    }
    return orphans;
}

} // namespace threadmaxx::migration
