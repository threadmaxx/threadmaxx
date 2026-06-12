#pragma once

/// @file panels/world_diff.hpp
/// @brief `WorldDiffPanel` — wraps editor's E9 `WorldDiff` as an
/// `IStudioPanel`. Holds a named-slot snapshot picker; computeDiff
/// runs the diff between two stored slots; render emits one row per
/// `WorldDiffEntry`.

#include "../panel.hpp"

#include <threadmaxx_editor/diff.hpp>

#include <threadmaxx/Serialization.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace threadmaxx::studio {

class WorldDiffPanel : public IStudioPanel {
public:
    std::string_view id() const noexcept override {
        return "engine.world_diff";
    }
    std::string_view title() const noexcept override { return "WorldDiff"; }
    void render(editor::IEditorBackend& backend,
                IStudioDataSource& source) override;

    /// @brief Stash a snapshot in a named slot. Overwrites existing.
    void setSnapshot(std::string_view name, threadmaxx::WorldSnapshot snap);

    /// @brief Diff the two named slots and cache the result for
    /// `render` / `lastDiff`. Returns `false` if either slot is
    /// unknown.
    bool computeDiff(std::string_view fromSlot, std::string_view toSlot);

    /// @brief Most recently computed diff. Empty if no compute yet.
    std::span<const editor::WorldDiffEntry> lastDiff() const noexcept {
        return lastDiff_.entries;
    }

    /// @brief Number of populated slots.
    std::size_t slotCount() const noexcept { return slots_.size(); }

private:
    struct Slot {
        std::string name;
        threadmaxx::WorldSnapshot snap;
    };

    const threadmaxx::WorldSnapshot* findSlot(std::string_view name) const noexcept;

    std::vector<Slot> slots_;
    editor::WorldDiffResult lastDiff_{};
};

} // namespace threadmaxx::studio
