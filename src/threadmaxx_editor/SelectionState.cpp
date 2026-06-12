/// @file SelectionState.cpp

#include "threadmaxx_editor/selection.hpp"

#include <threadmaxx/World.hpp>

namespace threadmaxx::editor {

SelectionState::SelectionState(const threadmaxx::World& world) noexcept
    : world_(&world) {}

void SelectionState::select(threadmaxx::EntityHandle e) noexcept {
    sel_ = Selection{};
    sel_.kind = SelectionKind::Entity;
    sel_.entity = e;
}

void SelectionState::selectResource(std::uint64_t id) noexcept {
    sel_ = Selection{};
    sel_.kind = SelectionKind::Resource;
    sel_.resourceId = id;
}

void SelectionState::selectSystem(std::uint32_t index) noexcept {
    sel_ = Selection{};
    sel_.kind = SelectionKind::System;
    sel_.systemIndex = index;
}

void SelectionState::clear() noexcept {
    sel_ = Selection{};
}

Selection SelectionState::currentSelection() const noexcept {
    if (sel_.kind == SelectionKind::Entity) {
        if (!world_->alive(sel_.entity)) {
            sel_ = Selection{};
        }
    }
    return sel_;
}

} // namespace threadmaxx::editor
