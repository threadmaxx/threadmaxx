/// @file WorldDiff.cpp
/// @brief WorldSnapshot diff implementation.

#include "threadmaxx_editor/diff.hpp"

#include <threadmaxx/Components.hpp>

#include <cstring>
#include <string>
#include <unordered_map>

namespace threadmaxx::editor {

namespace {

bool transformsEqual(const threadmaxx::Transform& a,
                     const threadmaxx::Transform& b) noexcept {
    return std::memcmp(&a, &b, sizeof(threadmaxx::Transform)) == 0;
}

template <typename T>
bool podsEqual(const T& a, const T& b) noexcept {
    return std::memcmp(&a, &b, sizeof(T)) == 0;
}

// Index by EntityHandle.index for O(1) lookup.
std::unordered_map<std::uint32_t, std::size_t>
indexByEntityIndex(const threadmaxx::WorldSnapshot& s) {
    std::unordered_map<std::uint32_t, std::size_t> m;
    m.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        m.emplace(s.entities[i].index, i);
    }
    return m;
}

void compareAt(std::size_t ai, std::size_t bi,
               const threadmaxx::WorldSnapshot& a,
               const threadmaxx::WorldSnapshot& b,
               WorldDiffEntry& out) {
    if (!transformsEqual(a.transforms[ai], b.transforms[bi])) {
        out.componentChanges.emplace_back("Transform");
    }
    if (!podsEqual(a.velocities[ai], b.velocities[bi])) {
        out.componentChanges.emplace_back("Velocity");
    }
    if (!podsEqual(a.accelerations[ai], b.accelerations[bi])) {
        out.componentChanges.emplace_back("Acceleration");
    }
    if (!podsEqual(a.healths[ai], b.healths[bi])) {
        out.componentChanges.emplace_back("Health");
    }
    if (!podsEqual(a.factions[ai], b.factions[bi])) {
        out.componentChanges.emplace_back("Faction");
    }
    if (!podsEqual(a.userData[ai], b.userData[bi])) {
        out.componentChanges.emplace_back("UserData");
    }
    if (!podsEqual(a.masks[ai], b.masks[bi])) {
        out.componentChanges.emplace_back("ComponentMask");
    }
}

} // namespace

WorldDiffResult WorldDiff::compute(const threadmaxx::WorldSnapshot& a,
                                   const threadmaxx::WorldSnapshot& b) {
    WorldDiffResult result;

    const auto aIdx = indexByEntityIndex(a);
    const auto bIdx = indexByEntityIndex(b);

    // Removed: in a but not in b (or generation changed → removed+added).
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto h = a.entities[i];
        auto it = bIdx.find(h.index);
        if (it == bIdx.end()) {
            result.entries.push_back(
                {DiffKind::Removed, h, {}});
        } else if (b.entities[it->second].generation != h.generation) {
            result.entries.push_back(
                {DiffKind::Removed, h, {}});
        }
    }
    // Added or Modified: walk b.
    for (std::size_t j = 0; j < b.size(); ++j) {
        const auto h = b.entities[j];
        auto it = aIdx.find(h.index);
        if (it == aIdx.end()) {
            result.entries.push_back({DiffKind::Added, h, {}});
            continue;
        }
        if (a.entities[it->second].generation != h.generation) {
            // Re-allocated slot: treat as added.
            result.entries.push_back({DiffKind::Added, h, {}});
            continue;
        }
        // Same handle in both — compare component values.
        WorldDiffEntry e{};
        e.kind = DiffKind::Modified;
        e.handle = h;
        compareAt(it->second, j, a, b, e);
        if (!e.componentChanges.empty()) {
            result.entries.push_back(std::move(e));
        }
    }
    return result;
}

} // namespace threadmaxx::editor
