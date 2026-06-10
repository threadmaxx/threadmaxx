#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/obstacle.hpp"
#include "threadmaxx_navmesh/query.hpp"
#include "threadmaxx_navmesh/threadmaxx_navmesh.hpp"

#include <cmath>

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

// N8 — removing the obstacle restores the original (unobstructed)
// path verbatim. Belt-and-braces over the "blocks" test: confirms the
// overlay's `remove()` path actually drops the source obstacle AND
// every spatial-hash bucket it inhabited (no ghost cells left over).

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

    PathQueryService svc(reg, PathQueryService::Config{0});

    ObstacleOverlay overlay;
    DynamicObstacle wall;
    wall.center = Vec3{2.0f, 0.0f, 0.5f};
    wall.halfExtents = Vec3{0.6f, 1.0f, 0.6f};
    wall.height = 2.0f;
    const ObstacleId wallId = overlay.add(wall);
    CHECK(wallId != 0);

    PathRequest req;
    req.mesh = ref;
    req.start = Vec3{0.5f, 0.0f, 0.5f};
    req.goal  = Vec3{3.5f, 0.0f, 0.5f};
    req.allowPartial = false;
    req.obstacles = &overlay;

    // Blocked: detour through z=1.
    {
        PathId id = svc.request(req);
        CHECK(id != 0);
        auto maybe = svc.tryGet(id);
        CHECK(maybe.has_value());
        if (!maybe) return gTestFailures;
        CHECK_EQ(maybe->corridor.size(), std::size_t{6});
        CHECK(nearly(maybe->cost, 5.0f));
    }

    // Remove the obstacle — same request, same overlay pointer.
    overlay.remove(wallId);
    CHECK_EQ(overlay.obstacleCount(), std::size_t{0});

    {
        PathId id = svc.request(req);
        CHECK(id != 0);
        auto maybe = svc.tryGet(id);
        CHECK(maybe.has_value());
        if (!maybe) return gTestFailures;
        CHECK_EQ(maybe->corridor.size(), std::size_t{4});
        CHECK(nearly(maybe->cost, 3.0f));
    }

    // `remove()` on the same id again is a silent no-op.
    overlay.remove(wallId);
    CHECK_EQ(overlay.obstacleCount(), std::size_t{0});

    // Add a fresh obstacle (new id) and confirm `remove()` of the old
    // (stale) id doesn't disturb the live one.
    DynamicObstacle wall2 = wall;
    wall2.center = Vec3{2.0f, 0.0f, 1.5f}; // blocks row z=1 only
    const ObstacleId wall2Id = overlay.add(wall2);
    CHECK(wall2Id != wallId);
    overlay.remove(wallId); // stale id — no-op
    CHECK_EQ(overlay.obstacleCount(), std::size_t{1});

    // With z=1 blocked but z=0 free, the direct row-0 path still wins.
    {
        PathId id = svc.request(req);
        CHECK(id != 0);
        auto maybe = svc.tryGet(id);
        CHECK(maybe.has_value());
        if (!maybe) return gTestFailures;
        CHECK_EQ(maybe->corridor.size(), std::size_t{4});
        CHECK(nearly(maybe->cost, 3.0f));
    }

    EXIT_WITH_RESULT();
}
