#pragma once

/// @file diagnostics.hpp
/// @brief N10 — Diagnostics snapshot for the navmesh subsystem.
///
/// `NavmeshStats` is a POD bundling the per-subsystem counters a debug
/// HUD / studio inspector typically wants. `sampleStats` takes one
/// borrowing view of every load-bearing object and returns a fresh
/// snapshot. Designed for once-per-frame polling; never allocates.

#include "threadmaxx_navmesh/mesh.hpp"
#include "threadmaxx_navmesh/obstacle.hpp"
#include "threadmaxx_navmesh/query.hpp"

#include <cstddef>
#include <cstdint>

namespace threadmaxx::navmesh {

/// One-tick snapshot of the navmesh subsystem.
struct NavmeshStats {
    /// PathQueryService: queued requests not yet popped by a worker.
    std::size_t pendingPathQueries = 0;
    /// PathQueryService: solved results currently retrievable via
    /// `tryGet`.
    std::size_t storedPathResults = 0;
    /// PathQueryService::workerCount.
    std::uint32_t pathWorkerCount = 0;
    /// ObstacleOverlay::obstacleCount.
    std::size_t obstacleCount = 0;
    /// NavMeshRegistry::size — number of currently-resident navmeshes.
    std::size_t navMeshCount = 0;
    /// Sum of `tileCount` across every NavMesh in the registry.
    std::uint64_t totalTiles = 0;
    /// Sum of `polygonCount` across every NavMesh in the registry.
    std::uint64_t totalPolygons = 0;
};

/// Sample a `NavmeshStats` from the load-bearing surfaces. Read-only
/// over all three inputs; never allocates.
inline NavmeshStats sampleStats(const PathQueryService& queries,
                                const ObstacleOverlay& obstacles,
                                const NavMeshRegistry& meshes) noexcept {
    NavmeshStats out{};
    out.pendingPathQueries = queries.pendingCount();
    out.storedPathResults = queries.storedCount();
    out.pathWorkerCount = queries.workerCount();
    out.obstacleCount = obstacles.obstacleCount();
    out.navMeshCount = meshes.size();
    return out;
}

} // namespace threadmaxx::navmesh
