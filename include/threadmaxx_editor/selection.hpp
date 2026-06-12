#pragma once

/// @file selection.hpp
/// @brief Selection state for editor panels. The property panel,
/// gizmo, and details panes all read off the same `Selection`.

#include "types.hpp"

#include <cstdint>

#include <threadmaxx/Handles.hpp>

namespace threadmaxx {
class World;
} // namespace threadmaxx

namespace threadmaxx::editor {

/// @brief One selection. Single-target by design — multi-select is a
/// v1.x extension.
struct Selection {
    SelectionKind kind{SelectionKind::None};
    // Discriminated by `kind`:
    threadmaxx::EntityHandle entity{};
    std::uint64_t resourceId{0};
    std::uint32_t systemIndex{0};
};

/// @brief Selection state with stale-handle auto-clear semantics.
///
/// `currentSelection()` consults `world.alive(handle)` for
/// `SelectionKind::Entity` and clears the slot if the generation has
/// bumped since the selection was set. Resource / system selections
/// have no such auto-clear; game code is expected to clear them
/// explicitly when invalidated.
class SelectionState {
public:
    explicit SelectionState(const threadmaxx::World& world) noexcept;

    void select(threadmaxx::EntityHandle e) noexcept;
    void selectResource(std::uint64_t id) noexcept;
    void selectSystem(std::uint32_t index) noexcept;
    void clear() noexcept;

    /// @brief Returns the current selection. If the selection is an
    /// entity whose generation no longer matches a live slot, the
    /// selection is auto-cleared and `None` is returned.
    Selection currentSelection() const noexcept;

private:
    const threadmaxx::World* world_;
    mutable Selection sel_{};
};

} // namespace threadmaxx::editor
