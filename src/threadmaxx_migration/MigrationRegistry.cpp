/// @file MigrationRegistry.cpp
/// @brief M3 — implementation of MigrationRegistry.

#include <threadmaxx_migration/registry.hpp>

#include <algorithm>
#include <queue>
#include <utility>

namespace threadmaxx::migration {

namespace {

constexpr std::size_t kAliasResolveDepthCap = 64;

} // namespace

void MigrationRegistry::registerType(std::string typeName,
                                     SchemaVersion introduced) {
    auto [it, inserted] = types_.try_emplace(std::move(typeName));
    it->second.introduced = introduced;
}

bool MigrationRegistry::aliasType(std::string oldName, std::string newName) {
    if (oldName == newName) return false;
    if (oldName.empty() || newName.empty()) return false;

    // The new name must already be known (directly or via another alias).
    if (!knowsType(newName)) return false;

    // Idempotent: re-declaring the same alias is fine.
    auto existing = aliases_.find(oldName);
    if (existing != aliases_.end()) {
        return existing->second == newName;
    }

    // If oldName itself is already a registered canonical type,
    // refuse — we don't shadow concrete types with aliases.
    if (types_.find(oldName) != types_.end()) return false;

    // Cycle check: resolve newName forward; if the chain ever hits
    // oldName, the new alias would create a cycle.
    std::string_view cursor = newName;
    for (std::size_t depth = 0; depth < kAliasResolveDepthCap; ++depth) {
        if (cursor == oldName) return false;
        auto it = aliases_.find(std::string(cursor));
        if (it == aliases_.end()) break;
        cursor = it->second;
    }

    aliases_.emplace(std::move(oldName), std::move(newName));
    return true;
}

std::string MigrationRegistry::resolveOrEmpty_(std::string_view name) const {
    std::string_view cursor = name;
    for (std::size_t depth = 0; depth < kAliasResolveDepthCap; ++depth) {
        if (types_.find(std::string(cursor)) != types_.end()) {
            return std::string(cursor);
        }
        auto it = aliases_.find(std::string(cursor));
        if (it == aliases_.end()) return {};
        cursor = it->second;
    }
    return {};
}

std::string_view
MigrationRegistry::resolveAlias(std::string_view name) const noexcept {
    // Walk the alias graph; if we hit a registered type, that wins.
    // Otherwise return the input verbatim.
    std::string_view cursor = name;
    for (std::size_t depth = 0; depth < kAliasResolveDepthCap; ++depth) {
        if (types_.find(std::string(cursor)) != types_.end()) {
            // Safe to return: storage owned by `types_` keys.
            auto it = types_.find(std::string(cursor));
            return std::string_view{it->first};
        }
        auto it = aliases_.find(std::string(cursor));
        if (it == aliases_.end()) return name;
        cursor = it->second;
    }
    return name;
}

void MigrationRegistry::addMigration(std::string typeName,
                                    SchemaVersion from,
                                    SchemaVersion to,
                                    MigrationFn fn) {
    auto canonical = resolveOrEmpty_(typeName);
    if (canonical.empty()) {
        // Type was not registered yet; record it as introduced at
        // the migration's `from` version so subsequent calls work.
        registerType(typeName, from);
        canonical = typeName;
    }
    auto& info = types_[canonical];
    for (auto& step : info.steps) {
        if (step.from == from && step.to == to) {
            step.fn = std::move(fn);
            return;
        }
    }
    info.steps.push_back(MigrationStep{from, to, std::move(fn)});
}

bool MigrationRegistry::knowsType(std::string_view typeName) const noexcept {
    if (types_.find(std::string(typeName)) != types_.end()) return true;
    std::string_view cursor = typeName;
    for (std::size_t depth = 0; depth < kAliasResolveDepthCap; ++depth) {
        auto it = aliases_.find(std::string(cursor));
        if (it == aliases_.end()) return false;
        cursor = it->second;
        if (types_.find(std::string(cursor)) != types_.end()) return true;
    }
    return false;
}

SchemaVersion
MigrationRegistry::introducedAt(std::string_view typeName) const noexcept {
    auto canonical = resolveOrEmpty_(typeName);
    if (canonical.empty()) return SchemaVersion{};
    auto it = types_.find(canonical);
    if (it == types_.end()) return SchemaVersion{};
    return it->second.introduced;
}

bool MigrationRegistry::hasPath(std::string_view typeName,
                                SchemaVersion from,
                                SchemaVersion to) const noexcept {
    if (from == to) return true;
    auto canonical = resolveOrEmpty_(typeName);
    if (canonical.empty()) return false;
    auto it = types_.find(canonical);
    if (it == types_.end()) return false;

    // BFS over the migration step graph.
    std::queue<SchemaVersion> frontier;
    std::vector<SchemaVersion> visited;
    frontier.push(from);
    visited.push_back(from);
    while (!frontier.empty()) {
        const auto v = frontier.front();
        frontier.pop();
        for (const auto& step : it->second.steps) {
            if (step.from == v) {
                if (step.to == to) return true;
                if (std::find(visited.begin(), visited.end(), step.to) ==
                    visited.end()) {
                    visited.push_back(step.to);
                    frontier.push(step.to);
                }
            }
        }
    }
    return false;
}

const MigrationFn*
MigrationRegistry::findMigration(std::string_view typeName,
                                SchemaVersion from,
                                SchemaVersion to) const noexcept {
    auto canonical = resolveOrEmpty_(typeName);
    if (canonical.empty()) return nullptr;
    auto it = types_.find(canonical);
    if (it == types_.end()) return nullptr;
    for (const auto& step : it->second.steps) {
        if (step.from == from && step.to == to) return &step.fn;
    }
    return nullptr;
}

std::vector<MigrationRegistry::StepView>
MigrationRegistry::listSteps(std::string_view typeName) const {
    std::vector<StepView> out;
    auto canonical = resolveOrEmpty_(typeName);
    if (canonical.empty()) return out;
    auto it = types_.find(canonical);
    if (it == types_.end()) return out;
    out.reserve(it->second.steps.size());
    for (const auto& step : it->second.steps) {
        out.push_back({step.from, step.to});
    }
    return out;
}

} // namespace threadmaxx::migration
