#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/query.hpp"
#include "threadmaxx_navmesh/threadmaxx_navmesh.hpp"

#include <cmath>
#include <cstdint>

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

// N3 — area-mask filter. The fixture has a "water shortcut" tagged
// area 1; the dry detour over the top row is tag 0. Masking out area 1
// forces the longer path.

namespace {
bool nearly(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}
}

int main() {
    NavMeshRegistry reg;
    auto blob = makeAreaMaskStrip();
    NavMeshRef ref = reg.load(bytes(blob));
    CHECK(reg.isValid(ref));

    PathQueryService svc(reg);

    // Common request: from poly (0,0)=id 0 centroid to poly (2,0)=id 2 centroid.
    PathRequest req;
    req.mesh = ref;
    req.start = Vec3{0.5f, 0.0f, 0.5f};
    req.goal  = Vec3{2.5f, 0.0f, 0.5f};
    req.allowPartial = false;

    // Default mask — water shortcut allowed.
    req.areaMask = 0xFFFFFFFFu;
    PathId openId = svc.request(req);
    CHECK(openId != 0);
    auto open = svc.tryGet(openId);
    CHECK(open.has_value());
    if (!open) return gTestFailures;

    CHECK(open->success);
    CHECK(!open->partial);
    // Short path: 0 → 1 → 2, three polys, cost 2.0.
    CHECK_EQ(open->corridor.size(), std::size_t{3});
    CHECK(nearly(open->cost, 2.0f));
    CHECK_EQ(open->corridor[0].polyId, NavPolyId{0});
    CHECK_EQ(open->corridor[1].polyId, NavPolyId{1});
    CHECK_EQ(open->corridor[2].polyId, NavPolyId{2});

    // Mask out area 1 (the water bit). The shortcut poly 1 becomes
    // unwalkable; the solver must go around via the top row.
    req.areaMask = ~(std::uint32_t{1} << 1);  // every bit except 1.
    PathId dryId = svc.request(req);
    CHECK(dryId != 0);
    auto dry = svc.tryGet(dryId);
    CHECK(dry.has_value());
    if (!dry) return gTestFailures;

    CHECK(dry->success);
    CHECK(!dry->partial);
    // Long detour: 0 → 3 → 4 → 5 → 2, five polys, cost 4.0.
    CHECK_EQ(dry->corridor.size(), std::size_t{5});
    CHECK(nearly(dry->cost, 4.0f));
    CHECK_EQ(dry->corridor[0].polyId, NavPolyId{0});
    CHECK_EQ(dry->corridor[1].polyId, NavPolyId{3});
    CHECK_EQ(dry->corridor[2].polyId, NavPolyId{4});
    CHECK_EQ(dry->corridor[3].polyId, NavPolyId{5});
    CHECK_EQ(dry->corridor[4].polyId, NavPolyId{2});

    // Each corridor entry sits in the only tile.
    for (const auto& c : dry->corridor) {
        CHECK_EQ(c.tileId, NavTileId{0});
    }

    // N4 funnel smoothing collapses the detour around the water poly to
    // `[start, (1,0,1), (2,0,1), goal]` — two pinch corners at the
    // inner edges of the bypass.
    CHECK_EQ(dry->waypoints.size(), std::size_t{4});
    CHECK(nearly(dry->waypoints[1].x, 1.0f));
    CHECK(nearly(dry->waypoints[1].z, 1.0f));
    CHECK(nearly(dry->waypoints[2].x, 2.0f));
    CHECK(nearly(dry->waypoints[2].z, 1.0f));

    // The unobstructed shortcut path (water allowed) smooths to a
    // straight shot — start sits in poly 0, goal in poly 2 with the
    // water poly 1 between them, but the full strip is convex so the
    // funnel collapses to `[start, goal]`.
    CHECK_EQ(open->waypoints.size(), std::size_t{2});
    CHECK(nearly(open->waypoints.front().x, req.start.x));
    CHECK(nearly(open->waypoints.back().x, req.goal.x));

    EXIT_WITH_RESULT();
}
