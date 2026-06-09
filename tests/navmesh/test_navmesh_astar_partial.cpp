#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/query.hpp"
#include "threadmaxx_navmesh/threadmaxx_navmesh.hpp"

#include <cmath>

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

// N3 — with `allowPartial == true`, an unreachable goal yields a best-
// effort path that ends at the polygon with the smallest heuristic
// distance to the goal. For the two-islands fixture, the goal sits at
// x=10.5 in T1; the closest reachable polygon (starting from T0:0) is
// T0:1 — the +x neighbor of the start — whose centroid is closest to
// the goal among T0's four polygons.

namespace {
bool nearly(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}
}

int main() {
    NavMeshRegistry reg;
    auto blob = makeTwoDisconnectedTiles();
    NavMeshRef ref = reg.load(bytes(blob));
    CHECK(reg.isValid(ref));

    PathQueryService svc(reg);

    PathRequest req;
    req.mesh = ref;
    req.start = Vec3{0.5f, 0.0f, 0.5f};   // T0 poly (0,0).
    req.goal  = Vec3{10.5f, 0.0f, 0.5f};  // T1 poly (0,0) — unreachable.
    req.allowPartial = true;

    PathId id = svc.request(req);
    CHECK(id != 0);
    auto res = svc.tryGet(id);
    CHECK(res.has_value());
    if (!res) return gTestFailures;

    CHECK(res->ready);
    CHECK(res->success);
    CHECK(res->partial);

    // Best-effort path ends at T0 poly (1,0)=id 1 — its centroid
    // (1.5, 0, 0.5) is the closest of the four T0 polys to the goal.
    CHECK(!res->corridor.empty());
    CHECK_EQ(res->corridor.front().tileId, NavTileId{0});
    CHECK_EQ(res->corridor.front().polyId, NavPolyId{0});
    CHECK_EQ(res->corridor.back().tileId, NavTileId{0});
    CHECK_EQ(res->corridor.back().polyId, NavPolyId{1});

    // One hop = corridor size 2 = cost 1.0 (single unit edge).
    CHECK_EQ(res->corridor.size(), std::size_t{2});
    CHECK(nearly(res->cost, 1.0f));

    // Waypoints: start + 1 midpoint + end-anchor (centroid of T0:1).
    CHECK_EQ(res->waypoints.size(), std::size_t{3});
    CHECK(nearly(res->waypoints.front().x, req.start.x));
    CHECK(nearly(res->waypoints.front().z, req.start.z));
    // Final waypoint is the partial endpoint — T0:1 centroid (1.5, 0, 0.5).
    CHECK(nearly(res->waypoints.back().x, 1.5f));
    CHECK(nearly(res->waypoints.back().z, 0.5f));

    // Re-issue with `allowPartial=false`: must fail.
    PathRequest strict = req;
    strict.allowPartial = false;
    PathId strictId = svc.request(strict);
    auto strictRes = svc.tryGet(strictId);
    CHECK(strictRes.has_value());
    if (strictRes) {
        CHECK(!strictRes->success);
        CHECK(strictRes->corridor.empty());
    }

    EXIT_WITH_RESULT();
}
