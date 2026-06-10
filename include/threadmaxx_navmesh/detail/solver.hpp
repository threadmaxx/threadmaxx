#pragma once

#include "threadmaxx_navmesh/detail/a_star.hpp"
#include "threadmaxx_navmesh/detail/funnel.hpp"
#include "threadmaxx_navmesh/mesh.hpp"
#include "threadmaxx_navmesh/query.hpp"

#include "threadmaxx/Components.hpp"

#include <cstdint>
#include <optional>
#include <vector>

/// Shared solver internals — the per-request A* + funnel pipeline used
/// by both `PathQueryService` (N5, single-request async queue) and
/// `BatchPathSolver` (N6, fan-out over a request list). Keeping the
/// solver here lets both surfaces share a single tested code path.
namespace threadmaxx::navmesh::detail {

/// `(tileIdx, polyId)` the way the solver wants it — `tileIdx` is the
/// index into `NavMesh::tiles()`, NOT the tile id. Returned by `locate`.
struct PolyLocation {
    std::uint32_t tileIdx{};
    NavPolyId polyId{};
};

/// Reusable A* + funnel scratch. One instance per worker thread; every
/// member preserves capacity across calls.
struct SolverScratch {
    AStarState state;
    NodeIndex nodeIndex;
    std::vector<Vec3> centroids;
    std::vector<FunnelPortal> portalBuf;
};

/// Pre-validated request: start/goal already located on the mesh.
struct PreparedRequest {
    PathId id{};
    PathRequest req{};
    PolyLocation startLoc{};
    PolyLocation goalLoc{};
};

/// Resolve a world-space XZ position to the polygon containing it.
/// `std::nullopt` when no polygon contains the point.
std::optional<PolyLocation> locate(const NavMesh& mesh, const Vec3& q);

/// Run A* + funnel smoothing on `prep` against `mesh`. `out` is fully
/// reset and overwritten. `scratch` retains capacity for the next call.
void solvePrepared(const NavMesh& mesh,
                   const PreparedRequest& prep,
                   SolverScratch& scratch,
                   PathResult& out);

} // namespace threadmaxx::navmesh::detail
