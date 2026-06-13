/// @file Hierarchy.cpp
/// @brief E12 — scene-hierarchy view over `Parent`-chained entities.

#include "threadmaxx_editor/hierarchy.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx/World.hpp>

#include <string>
#include <unordered_map>

namespace threadmaxx::editor {

namespace {

std::string labelFor(threadmaxx::EntityHandle h) {
    return std::string("entity#") + std::to_string(h.index);
}

bool isLiveParent(const threadmaxx::World& world,
                  threadmaxx::EntityHandle parent,
                  threadmaxx::EntityHandle self) {
    if (!parent.valid()) return false;
    if (parent == self) return false;
    return world.alive(parent);
}

} // namespace

HierarchyView::HierarchyView(threadmaxx::Engine& engine) noexcept
    : engine_(&engine) {}

std::vector<HierarchyNode> HierarchyView::tree() const {
    const auto& world = engine_->world();
    const auto handles = world.entities();

    // Index handles by parent for O(1) child lookup. Roots collect
    // into a separate list so we keep the world's entity order.
    std::unordered_map<threadmaxx::EntityHandle,
                       std::vector<threadmaxx::EntityHandle>> children;
    std::vector<threadmaxx::EntityHandle> roots;
    roots.reserve(handles.size());

    for (auto h : handles) {
        if (const auto* p = world.tryGetParent(h);
            p != nullptr && isLiveParent(world, p->parent, h)) {
            children[p->parent].push_back(h);
        } else {
            roots.push_back(h);
        }
    }

    std::vector<HierarchyNode> out;
    out.reserve(handles.size());

    std::unordered_set<threadmaxx::EntityHandle> visited;
    visited.reserve(handles.size());

    // Iterative DFS with explicit stack so cycles can't blow the
    // call stack. `(handle, depth, skipDescendants)` triples.
    struct Frame {
        threadmaxx::EntityHandle handle;
        std::uint32_t            depth;
    };
    std::vector<Frame> stack;
    stack.reserve(handles.size());

    // Push roots in reverse so DFS emits them in world order.
    for (auto it = roots.rbegin(); it != roots.rend(); ++it) {
        stack.push_back(Frame{*it, 0});
    }

    while (!stack.empty()) {
        Frame f = stack.back();
        stack.pop_back();
        if (!visited.insert(f.handle).second) continue;

        const auto childIt = children.find(f.handle);
        const bool hasKids =
            childIt != children.end() && !childIt->second.empty();
        const bool expanded = collapsed_.find(f.handle) == collapsed_.end();

        HierarchyNode node;
        node.handle      = f.handle;
        node.depth       = f.depth;
        node.label       = labelFor(f.handle);
        node.hasChildren = hasKids;
        node.expanded    = expanded;
        out.push_back(std::move(node));

        if (hasKids && expanded) {
            const auto& kids = childIt->second;
            for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
                stack.push_back(Frame{*it, f.depth + 1});
            }
        }
    }

    return out;
}

void HierarchyView::setExpanded(threadmaxx::EntityHandle handle,
                                bool expanded) {
    if (expanded) {
        collapsed_.erase(handle);
    } else {
        collapsed_.insert(handle);
    }
}

bool HierarchyView::isExpanded(threadmaxx::EntityHandle handle) const {
    return collapsed_.find(handle) == collapsed_.end();
}

std::size_t HierarchyView::collapsedCount() const noexcept {
    return collapsed_.size();
}

} // namespace threadmaxx::editor
