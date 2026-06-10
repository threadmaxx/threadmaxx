#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

/// 2D (XZ-plane) spatial hash. Generic over the value type so the same
/// header can back the N8 obstacle overlay AND any future broadphase
/// (e.g. agent-vs-agent avoidance in v1.x).
///
/// Layout: bucket map keyed by `(cellX, cellZ)`, values stored as a
/// flat `std::vector` per cell. Each entry stores both the value AND
/// the source key the caller supplied to `insert`, so `removeKey`
/// can wipe every cell the source occupies without a separate index
/// structure.
///
/// Not thread-safe. The intended discipline is single-writer
/// (typically the game system that owns the obstacles) and zero
/// readers during a mutation; the navmesh solver consults the hash
/// only after the mutation phase has finished.
namespace threadmaxx::navmesh::detail {

/// Stable cell-coordinate POD. Negative coordinates are well-defined
/// (we always floor toward −∞, not toward zero).
struct CellCoord {
    std::int32_t x{};
    std::int32_t z{};

    bool operator==(const CellCoord& o) const noexcept {
        return x == o.x && z == o.z;
    }
};

struct CellCoordHash {
    std::size_t operator()(const CellCoord& c) const noexcept {
        // FNV-1a over the two ints — keeps the distribution good on
        // adjacent positive-coordinate cells where most game obstacles
        // live, and tolerates negative coordinates without bias.
        std::uint64_t h = 1469598103934665603ull;
        const auto mix = [&h](std::uint32_t v) {
            h ^= v;
            h *= 1099511628211ull;
        };
        mix(static_cast<std::uint32_t>(c.x));
        mix(static_cast<std::uint32_t>(c.z));
        return static_cast<std::size_t>(h);
    }
};

/// Floor-towards-negative-infinity int cast — the cell coordinate
/// math has to be consistent for negative-XZ points or obstacles
/// straddling the origin alias into the wrong bucket.
inline std::int32_t floorToInt(float v) noexcept {
    return static_cast<std::int32_t>(std::floor(v));
}

template <typename KeyT, typename ValueT>
class SpatialHashXZ {
public:
    explicit SpatialHashXZ(float cellSize) noexcept
        : cellSize_(cellSize > 0.0f ? cellSize : 1.0f),
          invCellSize_(1.0f / (cellSize > 0.0f ? cellSize : 1.0f)) {}

    /// Resolve a world-space XZ pair to its cell coordinate.
    CellCoord cellOf(float x, float z) const noexcept {
        return CellCoord{floorToInt(x * invCellSize_),
                         floorToInt(z * invCellSize_)};
    }

    /// Insert `value` (keyed by `key`) into every cell overlapping the
    /// XZ AABB `[minX, maxX] × [minZ, maxZ]`. The same key may be
    /// inserted multiple times before a `removeKey` — useful for
    /// "lazy rebuild" patterns where the caller doesn't track which
    /// cells the source previously occupied.
    void insertAabb(const KeyT& key, const ValueT& value,
                    float minX, float minZ,
                    float maxX, float maxZ) {
        const auto cMin = cellOf(minX, minZ);
        const auto cMax = cellOf(maxX, maxZ);
        for (std::int32_t cz = cMin.z; cz <= cMax.z; ++cz) {
            for (std::int32_t cx = cMin.x; cx <= cMax.x; ++cx) {
                cells_[CellCoord{cx, cz}].push_back(Entry{key, value});
            }
        }
    }

    /// Remove every entry whose key equals `key`. Empty cells are
    /// pruned so the bucket map's footprint shrinks with the obstacle
    /// set.
    void removeKey(const KeyT& key) {
        for (auto it = cells_.begin(); it != cells_.end();) {
            auto& bucket = it->second;
            bucket.erase(
                std::remove_if(bucket.begin(), bucket.end(),
                               [&key](const Entry& e) { return e.key == key; }),
                bucket.end());
            if (bucket.empty()) {
                it = cells_.erase(it);
            } else {
                ++it;
            }
        }
    }

    /// Drop every entry. Bucket allocation is released; capacity is
    /// not preserved (we don't expect overlays to clear every tick).
    void clear() noexcept {
        cells_.clear();
    }

    /// Apply `fn(value)` to every entry sitting in the cell that
    /// covers `(x, z)`. The callback is invoked with the stored value
    /// type by const-ref. Returns true on the first call where `fn`
    /// itself returns true; false if every entry returned false or the
    /// cell is empty. Designed for "first match wins" use cases like
    /// `isBlocked`.
    template <typename Fn>
    bool anyInCell(float x, float z, Fn&& fn) const {
        const auto it = cells_.find(cellOf(x, z));
        if (it == cells_.end()) return false;
        for (const Entry& e : it->second) {
            if (fn(e.value)) return true;
        }
        return false;
    }

    /// Cell side length (constructor argument, sanitized to be > 0).
    float cellSize() const noexcept { return cellSize_; }

    /// Total non-empty cell count. Diagnostic only.
    std::size_t cellCount() const noexcept { return cells_.size(); }

private:
    struct Entry {
        KeyT key;
        ValueT value;
    };

    float cellSize_;
    float invCellSize_;
    std::unordered_map<CellCoord, std::vector<Entry>, CellCoordHash> cells_;
};

} // namespace threadmaxx::navmesh::detail
