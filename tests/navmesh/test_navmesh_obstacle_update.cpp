#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/obstacle.hpp"
#include "threadmaxx_navmesh/query.hpp"
#include "threadmaxx_navmesh/threadmaxx_navmesh.hpp"

#include <cmath>

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

// N8 — moving an obstacle invalidates the cells it used to occupy and
// the cells it now occupies. The same `PathRequest` issued before and
// after the `update()` call returns paths that reflect the new state.

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

    // Initial blocked state — wall sits across the middle of row 0.
    {
        PathId id = svc.request(req);
        CHECK(id != 0);
        auto maybe = svc.tryGet(id);
        CHECK(maybe.has_value());
        if (!maybe) return gTestFailures;
        CHECK_EQ(maybe->corridor.size(), std::size_t{6});
        CHECK(nearly(maybe->cost, 5.0f));
    }

    // Move the obstacle far away. Same request, same overlay pointer —
    // the path now goes through the previously-blocked cells.
    DynamicObstacle moved = wall;
    moved.center = Vec3{50.0f, 0.0f, 50.0f};
    overlay.update(wallId, moved);
    CHECK_EQ(overlay.obstacleCount(), std::size_t{1});

    {
        PathId id = svc.request(req);
        CHECK(id != 0);
        auto maybe = svc.tryGet(id);
        CHECK(maybe.has_value());
        if (!maybe) return gTestFailures;
        CHECK_EQ(maybe->corridor.size(), std::size_t{4});
        CHECK(nearly(maybe->cost, 3.0f));
    }

    // Move the obstacle so it now blocks the upper row z=1 instead.
    // The path was free to use z=1 before; after the update it must
    // route through z=0 (which is now free since the wall moved out).
    DynamicObstacle upRow = wall;
    upRow.center = Vec3{2.0f, 0.0f, 1.5f};
    overlay.update(wallId, upRow);

    {
        PathId id = svc.request(req);
        CHECK(id != 0);
        auto maybe = svc.tryGet(id);
        CHECK(maybe.has_value());
        if (!maybe) return gTestFailures;
        // z=0 row is unobstructed again, so the direct path is the
        // cheapest option. (The fact that z=1 is now blocked doesn't
        // matter because the direct route doesn't use it.)
        CHECK_EQ(maybe->corridor.size(), std::size_t{4});
        CHECK(nearly(maybe->cost, 3.0f));
    }

    EXIT_WITH_RESULT();
}
