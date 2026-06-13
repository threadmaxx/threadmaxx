/// @file MigrationPipeline.cpp
/// @brief M5 — MigrationPipeline. Walks the per-type chain via BFS;
/// records intermediate steps; honours MigrationOptions.

#include <threadmaxx_migration/pipeline.hpp>

#include <algorithm>
#include <cstdio>
#include <queue>
#include <string>
#include <unordered_map>

namespace threadmaxx::migration {

namespace {

std::string formatSchemaVersion(const SchemaVersion& v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u", v.major, v.minor, v.patch);
    return buf;
}

// Best-effort BFS that returns the chain of intermediate versions
// (including @p from and @p to) when a path exists. Returns empty
// when no path exists.
std::vector<SchemaVersion>
findChain(const MigrationRegistry& reg,
          std::string_view typeName,
          SchemaVersion from,
          SchemaVersion to) {
    if (from == to) return {from};
    auto steps = reg.listSteps(typeName);
    if (steps.empty()) return {};

    // BFS, tracking parents for path reconstruction.
    std::unordered_map<std::uint64_t, SchemaVersion> parent;
    auto key = [](const SchemaVersion& v) {
        return (static_cast<std::uint64_t>(v.major) << 40) |
               (static_cast<std::uint64_t>(v.minor) << 20) |
               static_cast<std::uint64_t>(v.patch);
    };
    std::queue<SchemaVersion> frontier;
    frontier.push(from);
    parent[key(from)] = from;
    bool reached = false;
    while (!frontier.empty()) {
        const auto v = frontier.front();
        frontier.pop();
        for (const auto& step : steps) {
            if (step.from == v) {
                if (parent.find(key(step.to)) != parent.end()) continue;
                parent[key(step.to)] = v;
                if (step.to == to) {
                    reached = true;
                    break;
                }
                frontier.push(step.to);
            }
        }
        if (reached) break;
    }
    if (!reached) return {};

    // Reconstruct.
    std::vector<SchemaVersion> chain;
    SchemaVersion cursor = to;
    while (true) {
        chain.push_back(cursor);
        if (cursor == from) break;
        auto it = parent.find(key(cursor));
        if (it == parent.end()) return {};
        cursor = it->second;
    }
    std::reverse(chain.begin(), chain.end());
    return chain;
}

SchemaVersion latestVersion(const MigrationRegistry& reg,
                            std::string_view typeName) {
    auto steps = reg.listSteps(typeName);
    SchemaVersion latest = reg.introducedAt(typeName);
    for (const auto& s : steps) {
        if (latest < s.from) latest = s.from;
        if (latest < s.to)   latest = s.to;
    }
    return latest;
}

bool extractLossyMarker(Record& rec) {
    auto it = std::find_if(rec.fields.begin(), rec.fields.end(),
                           [](const RecordField& f) {
                               return f.name == kLossyMarkerField;
                           });
    if (it == rec.fields.end()) return false;
    rec.fields.erase(it);
    return true;
}

} // namespace

MigrationResult
MigrationPipeline::migrate(const RecordSet& input,
                           SchemaVersion target,
                           const MigrationOptions& options) const {
    MigrationResult result{};
    result.output.metadata = input.metadata;
    result.output.metadata.schemaVersion = target;
    result.output.records.reserve(input.records.size());

    for (const auto& sourceRec : input.records) {
        Record rec = sourceRec;
        const std::uint64_t originalId = rec.stableId;

        if (!registry_.knowsType(rec.typeName)) {
            if (options.failOnUnknownType) {
                result.errors.push_back(
                    "unknown type: " + rec.typeName);
                result.output.records.push_back(std::move(rec));
                return result;
            }
            // Opaque tombstone path.
            result.warnings.push_back(
                "unknown type kept as opaque tombstone: " + rec.typeName);
            result.output.records.push_back(std::move(rec));
            continue;
        }

        // Find a path from the record's sourceVersion → target.
        const auto chain = findChain(registry_, rec.typeName,
                                     rec.sourceVersion, target);
        if (chain.empty()) {
            std::string err = "no migration path for type " + rec.typeName +
                              " from " + formatSchemaVersion(rec.sourceVersion) +
                              " to " + formatSchemaVersion(target);
            result.errors.push_back(std::move(err));
            result.output.records.push_back(std::move(rec));
            return result;
        }

        // Walk the chain step-by-step.
        for (std::size_t i = 1; i < chain.size(); ++i) {
            const auto from = chain[i - 1];
            const auto to   = chain[i];
            const auto* fn  = registry_.findMigration(rec.typeName, from, to);
            if (fn == nullptr || !*fn) {
                std::string err = "missing migration body for " + rec.typeName +
                                  " from " + formatSchemaVersion(from) +
                                  " to " + formatSchemaVersion(to);
                result.errors.push_back(std::move(err));
                result.output.records.push_back(std::move(rec));
                return result;
            }
            (*fn)(rec);

            if (extractLossyMarker(rec)) {
                std::string msg = "lossy migration: " + rec.typeName +
                                  " " + formatSchemaVersion(from) +
                                  " -> " + formatSchemaVersion(to);
                if (options.allowLossy) {
                    result.warnings.push_back(std::move(msg));
                } else {
                    result.errors.push_back(std::move(msg));
                    result.output.records.push_back(std::move(rec));
                    return result;
                }
            }

            if (options.preserveStableIds && rec.stableId != originalId) {
                std::string err = "stableId changed during migration of " +
                                  rec.typeName +
                                  " from " + formatSchemaVersion(from) +
                                  " to " + formatSchemaVersion(to);
                result.errors.push_back(std::move(err));
                result.output.records.push_back(std::move(rec));
                return result;
            }

            std::string label = rec.typeName + " " +
                                formatSchemaVersion(from) +
                                " -> " + formatSchemaVersion(to);
            result.appliedSteps.push_back(std::move(label));
        }

        rec.sourceVersion = target;
        result.output.records.push_back(std::move(rec));
    }

    result.ok = true;
    return result;
}

MigrationResult
MigrationPipeline::migrateToLatest(const RecordSet& input,
                                  const MigrationOptions& options) const {
    MigrationResult result{};
    result.output.metadata = input.metadata;
    result.output.records.reserve(input.records.size());

    SchemaVersion highestTarget{};

    for (const auto& sourceRec : input.records) {
        Record rec = sourceRec;
        const std::uint64_t originalId = rec.stableId;

        if (!registry_.knowsType(rec.typeName)) {
            if (options.failOnUnknownType) {
                result.errors.push_back(
                    "unknown type: " + rec.typeName);
                result.output.records.push_back(std::move(rec));
                return result;
            }
            result.warnings.push_back(
                "unknown type kept as opaque tombstone: " + rec.typeName);
            result.output.records.push_back(std::move(rec));
            continue;
        }

        const auto target = latestVersion(registry_, rec.typeName);
        if (highestTarget < target) highestTarget = target;

        const auto chain = findChain(registry_, rec.typeName,
                                     rec.sourceVersion, target);
        if (chain.empty() && rec.sourceVersion != target) {
            std::string err = "no migration path for type " + rec.typeName +
                              " from " + formatSchemaVersion(rec.sourceVersion) +
                              " to " + formatSchemaVersion(target);
            result.errors.push_back(std::move(err));
            result.output.records.push_back(std::move(rec));
            return result;
        }

        for (std::size_t i = 1; i < chain.size(); ++i) {
            const auto from = chain[i - 1];
            const auto to   = chain[i];
            const auto* fn  = registry_.findMigration(rec.typeName, from, to);
            if (fn == nullptr || !*fn) {
                result.errors.push_back(
                    "missing migration body for " + rec.typeName);
                result.output.records.push_back(std::move(rec));
                return result;
            }
            (*fn)(rec);

            if (extractLossyMarker(rec)) {
                std::string msg = "lossy migration: " + rec.typeName +
                                  " " + formatSchemaVersion(from) +
                                  " -> " + formatSchemaVersion(to);
                if (options.allowLossy) {
                    result.warnings.push_back(std::move(msg));
                } else {
                    result.errors.push_back(std::move(msg));
                    result.output.records.push_back(std::move(rec));
                    return result;
                }
            }

            if (options.preserveStableIds && rec.stableId != originalId) {
                result.errors.push_back(
                    "stableId changed during migration of " + rec.typeName);
                result.output.records.push_back(std::move(rec));
                return result;
            }

            std::string label = rec.typeName + " " +
                                formatSchemaVersion(from) +
                                " -> " + formatSchemaVersion(to);
            result.appliedSteps.push_back(std::move(label));
        }

        rec.sourceVersion = target;
        result.output.records.push_back(std::move(rec));
    }

    // Bump the bundle metadata to the highest observed target.
    if (input.metadata.schemaVersion < highestTarget) {
        result.output.metadata.schemaVersion = highestTarget;
    }
    result.ok = true;
    return result;
}

} // namespace threadmaxx::migration
