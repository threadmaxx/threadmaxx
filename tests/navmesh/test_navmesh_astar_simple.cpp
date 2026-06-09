#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/query.hpp"
#include "threadmaxx_navmesh/threadmaxx_navmesh.hpp"

#include <cmath>
#include <cstdint>

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

// N3 — synchronous A* across the L-shape fixture. Start at the (0,0)
// corner of T0; goal at the far (1,1) corner of T3. Either traversal
// (row 0 first or row 1 first) walks 7 polygons across 3 tiles for a
// total graph cost of 6.0 (six unit-length centroid-to-centroid edges).
//
// The test asserts the cost + corridor count rather than a specific
// poly sequence, since ties along the row swap freely.

namespace {
bool nearly(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}
}

int main() {
    NavMeshRegistry reg;
    auto blob = make4TileLShape();
    NavMeshRef ref = reg.load(bytes(blob));
    CHECK(reg.isValid(ref));

    PathQueryService svc(reg);

    PathRequest req;
    req.mesh = ref;
    req.start = Vec3{0.5f, 0.0f, 0.5f};   // T0 poly (0,0) centroid.
    req.goal  = Vec3{5.5f, 0.0f, 1.5f};   // T3 poly (1,1) centroid.
    req.allowPartial = false;

    PathId id = svc.request(req);
    CHECK(id != 0);
    CHECK(svc.lastRequestStatus() == PathRequestStatus::Accepted);

    auto maybe = svc.tryGet(id);
    CHECK(maybe.has_value());
    if (!maybe) return gTestFailures;
    const PathResult& r = *maybe;

    CHECK(r.ready);
    CHECK(r.success);
    CHECK(!r.partial);

    // 7 polygons traversed = 6 edges = cost 6.0.
    CHECK_EQ(r.corridor.size(), std::size_t{7});
    CHECK(nearly(r.cost, 6.0f));

    // Endpoints match the request.
    CHECK(r.waypoints.size() >= 2);
    CHECK(nearly(r.waypoints.front().x, req.start.x));
    CHECK(nearly(r.waypoints.front().z, req.start.z));
    CHECK(nearly(r.waypoints.back().x, req.goal.x));
    CHECK(nearly(r.waypoints.back().z, req.goal.z));

    // First corridor entry MUST be the start polygon; last is the goal.
    CHECK_EQ(r.corridor.front().tileId, NavTileId{0});
    CHECK_EQ(r.corridor.front().polyId, NavPolyId{0});
    CHECK_EQ(r.corridor.back().tileId, NavTileId{3});
    CHECK_EQ(r.corridor.back().polyId, NavPolyId{3});

    // The full waypoint list is start + (corridor.size()-1) edge midpoints + goal.
    CHECK_EQ(r.waypoints.size(), r.corridor.size() + 1);

    // Same-poly query degenerates to [start, goal] with cost 0.
    PathRequest selfReq = req;
    selfReq.start = Vec3{2.5f, 0.0f, 0.5f};
    selfReq.goal  = Vec3{2.5f, 0.0f, 0.5f};
    PathId selfId = svc.request(selfReq);
    CHECK(selfId != 0);
    auto selfRes = svc.tryGet(selfId);
    CHECK(selfRes.has_value());
    if (selfRes) {
        CHECK(selfRes->success);
        CHECK_EQ(selfRes->corridor.size(), std::size_t{1});
        CHECK(nearly(selfRes->cost, 0.0f));
        CHECK_EQ(selfRes->waypoints.size(), std::size_t{2});
    }

    // tryGet on an unknown id is empty.
    CHECK(!svc.tryGet(0xDEADBEEF).has_value());

    // cancel + tryGet → empty.
    svc.cancel(id);
    CHECK(!svc.tryGet(id).has_value());

    // clear drops everything.
    svc.clear();
    CHECK(!svc.tryGet(selfId).has_value());
    CHECK_EQ(svc.storedCount(), std::size_t{0});

    EXIT_WITH_RESULT();
}
