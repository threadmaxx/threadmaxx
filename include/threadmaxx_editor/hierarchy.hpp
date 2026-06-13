#pragma once

/// @file hierarchy.hpp
/// @brief E12 — scene-hierarchy view over `Parent`-chained entities.
///
/// `HierarchyView` is a polled, read-only walk of the engine's
/// entity set, grouped by `Parent::parent`. `tree()` returns rows in
/// DFS order; collapsed nodes hide their descendants. Game-side
/// expand/collapse state lives in the view (panels don't have to
/// persist it themselves).

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Handles.hpp>

namespace threadmaxx::editor {

/// @brief One row of `HierarchyView::tree()`. Studio's
/// HierarchyPanel renders one entry per node.
struct HierarchyNode {
    threadmaxx::EntityHandle handle{};
    std::uint32_t            depth{0};
    std::string              label;
    bool                     hasChildren{false};
    /// True iff this node is currently expanded. Collapsed nodes
    /// still appear in `tree()` (so the panel can render the row
    /// with a chevron); their descendants are omitted.
    bool                     expanded{true};
};

/// @brief Scene-hierarchy view over a live `Engine`.
///
/// Cycle-safe: `Parent` references that loop are broken in DFS
/// (the second visit to an entity within one tree() call is
/// suppressed). Roots are entities without `Parent`, plus those
/// whose `Parent::parent` is invalid / stale / a self-reference.
///
/// @thread_safety Sim-thread by convention. `tree()` snapshots
///                world state at call time; do not invoke from a
///                tight UI loop.
class HierarchyView {
public:
    explicit HierarchyView(threadmaxx::Engine& engine) noexcept;

    /// @brief Snapshot the current hierarchy in DFS order. The
    /// returned vector is caller-owned.
    [[nodiscard]] std::vector<HierarchyNode> tree() const;

    /// @brief Mark a node collapsed. Future `tree()` calls omit its
    /// descendants. No-op for unknown / never-collapsed handles
    /// when @p expanded is true.
    void setExpanded(threadmaxx::EntityHandle handle, bool expanded);

    /// @brief True iff the node is currently expanded (default).
    [[nodiscard]] bool isExpanded(threadmaxx::EntityHandle handle) const;

    /// @brief Count of nodes the view currently marks collapsed.
    /// Used by panels for "Expand All" affordances.
    [[nodiscard]] std::size_t collapsedCount() const noexcept;

private:
    threadmaxx::Engine* engine_;
    std::unordered_set<threadmaxx::EntityHandle> collapsed_;
};

} // namespace threadmaxx::editor
