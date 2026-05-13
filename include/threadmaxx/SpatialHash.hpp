#pragma once

#include "Components.hpp"

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace threadmaxx {

/// @file SpatialHash.hpp
/// Uniform-grid spatial index keyed by 3D cell coordinate.
///
/// Insert (entity, position) pairs into a hash-of-cells, then query by
/// radius or axis-aligned box. Useful for neighbor lookups, broadphase
/// physics, AOI for streaming, range-based AI targeting — anything that
/// would otherwise require an `O(n²)` scan over entities.
///
/// The grid is cleared and rebuilt by the caller; the engine does not
/// own it (the FUTURE_WORK §3.4 contract). Rebuilding typically lives in
/// a `preStep` hook so wave systems see the just-rebuilt index, but it
/// is also fine to rebuild from a `single()` callback inside a system
/// that reads `Transform` and writes nothing else.
///
/// @tparam Payload Value stored alongside the position. Most callers
///                 want `EntityHandle`; physics broadphase might use a
///                 narrower body index. Required: trivially copyable
///                 and equality-comparable.
template <typename Payload>
class SpatialHash {
public:
    /// Construct with the cubic cell side length. Pick this near the
    /// typical query radius — too small inflates the per-query cell
    /// count, too large pulls in too many false-positive candidates.
    /// @pre `cellSize > 0`. Non-positive sizes are clamped to 1.
    explicit SpatialHash(float cellSize) noexcept
        : cellSize_(cellSize > 0.0f ? cellSize : 1.0f)
        , invCellSize_(1.0f / (cellSize > 0.0f ? cellSize : 1.0f)) {}

    /// Drop all entries while keeping the bucket map allocation.
    /// Steady-state per-tick rebuild costs zero allocations.
    void clear() noexcept {
        for (auto& [k, v] : cells_) v.clear();
        size_ = 0;
    }

    /// Insert one payload at a world-space position.
    void insert(const Vec3& position, Payload payload) {
        const auto key = cellKey(position);
        cells_[key].push_back({position, std::move(payload)});
        ++size_;
    }

    /// Total number of inserted entries since the last @ref clear.
    std::size_t size() const noexcept { return size_; }

    /// Number of populated cells (for tuning the cell size).
    std::size_t cellCount() const noexcept { return cells_.size(); }

    /// Side length of one cell, in world units.
    float cellSize() const noexcept { return cellSize_; }

    /// Invoke @p fn on every payload within `radius` of @p center.
    ///
    /// `fn` is called with `(const Vec3& position, const Payload&)`.
    /// Distance is full 3D Euclidean (not cylindrical). Walks every
    /// cell whose AABB intersects the query sphere, then does an
    /// exact distance² test per candidate.
    template <typename F>
    void forEachInRadius(const Vec3& center, float radius, F&& fn) const {
        if (radius <= 0.0f) return;
        const float r2 = radius * radius;
        const Vec3 minP{center.x - radius, center.y - radius, center.z - radius};
        const Vec3 maxP{center.x + radius, center.y + radius, center.z + radius};
        const auto kMin = cellKey(minP);
        const auto kMax = cellKey(maxP);

        for (std::int32_t z = kMin.z; z <= kMax.z; ++z)
        for (std::int32_t y = kMin.y; y <= kMax.y; ++y)
        for (std::int32_t x = kMin.x; x <= kMax.x; ++x) {
            auto it = cells_.find({x, y, z});
            if (it == cells_.end()) continue;
            for (const auto& entry : it->second) {
                const float dx = entry.position.x - center.x;
                const float dy = entry.position.y - center.y;
                const float dz = entry.position.z - center.z;
                if (dx * dx + dy * dy + dz * dz <= r2) {
                    fn(entry.position, entry.payload);
                }
            }
        }
    }

    /// Invoke @p fn on every payload inside the axis-aligned box
    /// `[min, max]`. `fn` is called with `(const Vec3&, const Payload&)`.
    template <typename F>
    void forEachInBox(const Vec3& minP, const Vec3& maxP, F&& fn) const {
        const auto kMin = cellKey(minP);
        const auto kMax = cellKey(maxP);
        for (std::int32_t z = kMin.z; z <= kMax.z; ++z)
        for (std::int32_t y = kMin.y; y <= kMax.y; ++y)
        for (std::int32_t x = kMin.x; x <= kMax.x; ++x) {
            auto it = cells_.find({x, y, z});
            if (it == cells_.end()) continue;
            for (const auto& entry : it->second) {
                const auto& p = entry.position;
                if (p.x >= minP.x && p.x <= maxP.x &&
                    p.y >= minP.y && p.y <= maxP.y &&
                    p.z >= minP.z && p.z <= maxP.z) {
                    fn(p, entry.payload);
                }
            }
        }
    }

private:
    struct CellKey {
        std::int32_t x = 0;
        std::int32_t y = 0;
        std::int32_t z = 0;
        constexpr bool operator==(const CellKey&) const noexcept = default;
    };

    struct CellKeyHash {
        std::size_t operator()(const CellKey& k) const noexcept {
            // Splitmix-style spread; sufficient for typical scene scales.
            std::uint64_t h = static_cast<std::uint32_t>(k.x);
            h = (h << 21) ^ static_cast<std::uint32_t>(k.y);
            h = (h << 21) ^ static_cast<std::uint32_t>(k.z);
            h ^= h >> 33;
            h *= 0xff51afd7ed558ccdULL;
            h ^= h >> 33;
            return static_cast<std::size_t>(h);
        }
    };

    struct Entry {
        Vec3    position;
        Payload payload;
    };

    CellKey cellKey(const Vec3& p) const noexcept {
        return CellKey{
            static_cast<std::int32_t>(std::floor(p.x * invCellSize_)),
            static_cast<std::int32_t>(std::floor(p.y * invCellSize_)),
            static_cast<std::int32_t>(std::floor(p.z * invCellSize_)),
        };
    }

    float cellSize_;
    float invCellSize_;
    std::unordered_map<CellKey, std::vector<Entry>, CellKeyHash> cells_;
    std::size_t size_ = 0;
};

} // namespace threadmaxx
