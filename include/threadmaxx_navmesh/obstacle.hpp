#pragma once

#include "threadmaxx/Components.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

/// N8 — dynamic obstacle overlay.
///
/// Lets game code add temporary blockers without rebaking the navmesh.
/// `ObstacleOverlay` owns a spatial-hash index over a set of
/// `DynamicObstacle` AABBs in the XZ plane; A* consults the overlay
/// during edge expansion and refuses to cross into polygons whose
/// centroid sits inside any blocking obstacle.
///
/// The overlay is mutated from a single thread (typically the system
/// that owns gameplay obstacles) and queried by the solver — concurrent
/// `add`/`update`/`remove` against an in-flight `solvePrepared` is the
/// caller's responsibility to serialize. The typical recipe is:
///
///   1. Game system updates the overlay during `preStep`.
///   2. `PathRequest::obstacles` is set to the overlay pointer.
///   3. Path solving runs against a frozen snapshot of the overlay for
///      the rest of the tick.
namespace threadmaxx::navmesh {

using ::threadmaxx::Vec3;

/// Identifies a `DynamicObstacle` stored inside an `ObstacleOverlay`.
/// Zero is reserved as "invalid" — matches the `NavMeshId` / `PathId`
/// convention used elsewhere in the library.
using ObstacleId = std::uint64_t;

/// Renderer-neutral dynamic blocker. Treated as an axis-aligned XZ
/// rectangle by the runtime:
///   `[center.x − halfExtents.x, center.x + halfExtents.x] ×
///    [center.z − halfExtents.z, center.z + halfExtents.z]`.
/// The Y axis (`center.y`, `halfExtents.y`, `height`) is reserved for
/// v1.x 3D queries; the N8 overlay ignores it. `areaMask` selects the
/// polygon area tags this obstacle blocks; bit `k` blocks polygons
/// tagged `areaTag == k`. Default `0xFFFFFFFFu` blocks every area tag.
struct DynamicObstacle {
    Vec3 center{};
    Vec3 halfExtents{};
    float height{};
    std::uint32_t areaMask{0xFFFFFFFFu};
};

/// Tunables for the overlay. `cellSize` controls the spatial-hash grid
/// resolution — too coarse and small obstacles bucket into a single
/// (overfull) cell, too fine and large obstacles span hundreds of cells
/// and the per-mutation cost dominates. Default 1.0f is a reasonable
/// starting point for game-scale navmeshes; callers should reach for
/// `max(agentRadius, tileSize/8)` per the DESIGN_NOTES recommendation.
struct ObstacleOverlayConfig {
    float cellSize{1.0f};
};

/// Spatial-hash index of `DynamicObstacle`s. Header decorates the
/// PImpl'd implementation in `src/ObstacleOverlay.cpp`.
class ObstacleOverlay {
public:
    using Config = ObstacleOverlayConfig;

    explicit ObstacleOverlay(Config cfg = {});
    ~ObstacleOverlay();
    ObstacleOverlay(const ObstacleOverlay&) = delete;
    ObstacleOverlay& operator=(const ObstacleOverlay&) = delete;
    ObstacleOverlay(ObstacleOverlay&&) noexcept;
    ObstacleOverlay& operator=(ObstacleOverlay&&) noexcept;

    /// Insert `obstacle` and return a fresh non-zero id. Ids are
    /// strictly monotonic — removed ids are never reused.
    ObstacleId add(const DynamicObstacle& obstacle);

    /// Replace the obstacle stored under `id`. No-op for unknown id.
    /// The old spatial-hash buckets are cleared and the new ones built
    /// in a single call, so a moving obstacle never appears in both
    /// "old" and "new" cells mid-update.
    void update(ObstacleId id, const DynamicObstacle& obstacle);

    /// Remove the obstacle stored under `id`. No-op for unknown id.
    void remove(ObstacleId id);

    /// True if `xz` sits inside any obstacle whose `areaMask` shares a
    /// bit with `callerMask`. The y component is ignored. Cheap enough
    /// to call from the solver's per-edge expansion loop.
    bool isBlocked(const Vec3& xz,
                   std::uint32_t callerMask = 0xFFFFFFFFu) const noexcept;

    /// Number of live obstacles.
    std::size_t obstacleCount() const noexcept;

    /// Configured cell size. Exposed for tests / diagnostics.
    float cellSize() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace threadmaxx::navmesh
