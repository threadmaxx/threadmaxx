#pragma once

#include "threadmaxx_navmesh/detail/bitset.hpp"
#include "threadmaxx_navmesh/mesh.hpp"
#include "threadmaxx_navmesh/types.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <vector>

/// Internal A* state for the N3 path query service. Header-only so the
/// (future) batch solver can drop in a separate instance per worker
/// without dragging in `PathQueryService::Impl`.
namespace threadmaxx::navmesh::detail {

inline constexpr std::uint32_t kInvalidNode = 0xFFFFFFFFu;

/// Maps `(tileArrayIdx, polyId)` to a flat node id via tile-prefix
/// sums. Built per (mesh, request); a fresh mesh load may shift the
/// per-tile polygon counts.
struct NodeIndex {
    /// `tileOffsets[i]` = number of polygons in `mesh.tiles()[0..i)`.
    /// Final entry equals `mesh.polygonCount()` and tags the total
    /// node space.
    std::vector<std::uint32_t> tileOffsets;

    void rebuild(const NavMesh& mesh) {
        tileOffsets.assign(mesh.tiles().size() + 1, 0);
        for (std::size_t i = 0; i < mesh.tiles().size(); ++i) {
            tileOffsets[i + 1] =
                tileOffsets[i] +
                static_cast<std::uint32_t>(mesh.tiles()[i].polygons.size());
        }
    }

    /// Total node count across the mesh.
    std::uint32_t total() const noexcept { return tileOffsets.back(); }

    std::uint32_t encode(std::uint32_t tileIdx, NavPolyId polyId) const noexcept {
        return tileOffsets[tileIdx] + polyId;
    }

    /// Decompose a node back into `(tileIdx, polyId)`. Binary-search
    /// over `tileOffsets` — fine for tile counts in the hundreds.
    void decode(std::uint32_t node, std::uint32_t& tileIdx,
                NavPolyId& polyId) const noexcept {
        const auto it = std::upper_bound(
            tileOffsets.begin(), tileOffsets.end(), node);
        const auto pos = static_cast<std::uint32_t>(
            std::distance(tileOffsets.begin(), it));
        tileIdx = pos - 1u;
        polyId = node - tileOffsets[tileIdx];
    }
};

/// Single heap entry. `fCost = gCost + h(node, goal)`.
struct AStarHeapItem {
    std::uint32_t node{};
    float gCost{};
    float fCost{};
};

/// Binary min-heap on `fCost`. `std::push_heap` / `std::pop_heap` with
/// a max-heap comparator on the inverse.
struct AStarOpenSet {
    std::vector<AStarHeapItem> items;

    void clear() noexcept { items.clear(); }
    bool empty() const noexcept { return items.empty(); }

    void push(AStarHeapItem item) {
        items.push_back(item);
        std::push_heap(items.begin(), items.end(), cmp);
    }
    AStarHeapItem pop() {
        std::pop_heap(items.begin(), items.end(), cmp);
        AStarHeapItem top = items.back();
        items.pop_back();
        return top;
    }

    /// Strict-weak ordering for `std::push_heap` / `std::pop_heap` —
    /// returns true when `a` should sink BELOW `b`, i.e. `a` has the
    /// HIGHER `fCost`. The net effect is a min-heap by `fCost`.
    static bool cmp(const AStarHeapItem& a, const AStarHeapItem& b) noexcept {
        return a.fCost > b.fCost;
    }
};

/// Reusable A* scratch. `resize(maxNodes)` after `NodeIndex::rebuild`;
/// `reset()` between solves preserves the capacity.
struct AStarState {
    Bitset closed;
    std::vector<float> gCost;
    std::vector<std::uint32_t> cameFrom;
    AStarOpenSet open;

    void resize(std::size_t maxNodes) {
        closed.resize(maxNodes);
        gCost.assign(maxNodes, std::numeric_limits<float>::infinity());
        cameFrom.assign(maxNodes, kInvalidNode);
        open.clear();
    }

    void reset() noexcept {
        closed.clear();
        std::fill(gCost.begin(), gCost.end(),
                  std::numeric_limits<float>::infinity());
        std::fill(cameFrom.begin(), cameFrom.end(), kInvalidNode);
        open.clear();
    }
};

} // namespace threadmaxx::navmesh::detail
