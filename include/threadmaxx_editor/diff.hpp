#pragma once

/// @file diff.hpp
/// @brief Pairwise WorldSnapshot diff.
///
/// `WorldDiff::compute(a, b)` returns the set of entity-level
/// differences from `a` to `b`. Used by the editor's save-comparison
/// panel and by replay-attach diagnostics (v1.x). Diff is structural —
/// component-mask differences are categorized as `Modified` with the
/// per-bit changes spelled out; transform / velocity / etc. value
/// changes are also `Modified`.

#include <cstdint>
#include <string>
#include <vector>

#include <threadmaxx/Handles.hpp>
#include <threadmaxx/Serialization.hpp>

namespace threadmaxx::editor {

enum class DiffKind : std::uint8_t {
    Added,
    Removed,
    Modified,
};

/// @brief One entity-level diff entry. The lists of `componentChanges`
/// and `addedComponents` / `removedComponents` are non-empty for
/// `Modified` entries only.
struct WorldDiffEntry {
    DiffKind kind{DiffKind::Modified};
    threadmaxx::EntityHandle handle{};
    std::vector<std::string> componentChanges;
};

struct WorldDiffResult {
    std::vector<WorldDiffEntry> entries;

    bool empty() const noexcept { return entries.empty(); }
    std::size_t size() const noexcept { return entries.size(); }
};

class WorldDiff {
public:
    /// @brief Diff `a` → `b`. Entities are matched by
    /// `EntityHandle::index` (matching the engine's slot reuse model
    /// via generation bumps; same-index entries with different
    /// generations are reported as Removed + Added).
    static WorldDiffResult compute(const threadmaxx::WorldSnapshot& a,
                                   const threadmaxx::WorldSnapshot& b);
};

} // namespace threadmaxx::editor
