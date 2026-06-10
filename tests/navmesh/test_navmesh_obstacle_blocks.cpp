#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/obstacle.hpp"
#include "threadmaxx_navmesh/query.hpp"
#include "threadmaxx_navmesh/threadmaxx_navmesh.hpp"

#include <cmath>

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

// N8 — add a `DynamicObstacle` that bisects the straight-line path
// across the 4x4 flat-square fixture. The path is forced off the
// middle row of polys and routes around through z=1, so the corridor
// is strictly longer than the unobstructed path. Once the obstacle is
// removed, the corridor restores to the direct route.

namespace {
bool nearly(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}
} // namespace

int main() {
    NavMeshRegistry reg;
    auto blob = make16PolyFlatSquare();
    NavMeshRef ref = reg.load(bytes(blob));
    CHECK(reg.isValid(ref));

    // Synchronous mode — keeps the test deterministic without having
    // to coordinate the worker queue against overlay mutations.
    PathQueryService svc(reg, PathQueryService::Config{0});

    // (0,0) → (3,0): unobstructed path is 4 polygons along z=0 with
    // centroid-cost 3.0. Funnel-smoothed waypoints are `[start, goal]`
    // since the polys sit in a straight unobstructed strip.
    PathRequest baseReq;
    baseReq.mesh = ref;
    baseReq.start = Vec3{0.5f, 0.0f, 0.5f};
    baseReq.goal  = Vec3{3.5f, 0.0f, 0.5f};
    baseReq.allowPartial = false;

    {
        PathId id = svc.request(baseReq);
        CHECK(id != 0);
        auto maybe = svc.tryGet(id);
        CHECK(maybe.has_value());
        if (!maybe) return gTestFailures;
        CHECK(maybe->success);
        CHECK_EQ(maybe->corridor.size(), std::size_t{4});
        CHECK(nearly(maybe->cost, 3.0f));
    }

    // Stand up the overlay and drop a blocker centered between polys
    // (1,0) and (2,0). HalfExtents 0.6 covers both centroids ((1.5,
    // 0.5) and (2.5, 0.5)) so A* refuses to route through either.
    ObstacleOverlay overlay;
    DynamicObstacle wall;
    wall.center = Vec3{2.0f, 0.0f, 0.5f};
    wall.halfExtents = Vec3{0.6f, 1.0f, 0.6f};
    wall.height = 2.0f;
    const ObstacleId wallId = overlay.add(wall);
    CHECK(wallId != 0);
    CHECK_EQ(overlay.obstacleCount(), std::size_t{1});

    PathRequest blockedReq = baseReq;
    blockedReq.obstacles = &overlay;

    {
        PathId id = svc.request(blockedReq);
        CHECK(id != 0);
        auto maybe = svc.tryGet(id);
        CHECK(maybe.has_value());
        if (!maybe) return gTestFailures;
        CHECK(maybe->success);
        // Direct path through z=0 is blocked. Minimum detour walks
        // (0,0)→(0,1)→(1,1)→(2,1)→(3,1)→(3,0) — 6 polys at cost 5.0.
        CHECK_EQ(maybe->corridor.size(), std::size_t{6});
        CHECK(nearly(maybe->cost, 5.0f));

        // Sanity-check the blocked polys never appear in the corridor.
        for (const auto& entry : maybe->corridor) {
            CHECK(!(entry.tileId == NavTileId{0} && entry.polyId == NavPolyId{1}));
            CHECK(!(entry.tileId == NavTileId{0} && entry.polyId == NavPolyId{2}));
        }
    }

    // Drop the overlay (don't remove the obstacle — exercise the
    // `obstacles == nullptr` path explicitly). Corridor restores.
    {
        PathRequest restoredReq = baseReq;
        restoredReq.obstacles = nullptr;
        PathId id = svc.request(restoredReq);
        CHECK(id != 0);
        auto maybe = svc.tryGet(id);
        CHECK(maybe.has_value());
        if (!maybe) return gTestFailures;
        CHECK(maybe->success);
        CHECK_EQ(maybe->corridor.size(), std::size_t{4});
        CHECK(nearly(maybe->cost, 3.0f));
    }

    EXIT_WITH_RESULT();
}
