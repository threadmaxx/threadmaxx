#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/query.hpp"
#include "threadmaxx_navmesh/threadmaxx_navmesh.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

// N4 — funnel smoothing across a straight, unobstructed corridor.
// The 16-poly flat square is a 4x4 grid; the bottom row (polys 0,1,2,3)
// runs along z=0.5. The funnel collapses to `[start, goal]` regardless
// of the corridor poly count.

namespace {
bool nearly(float a, float b, float eps = 1e-3f) {
    return std::fabs(a - b) <= eps;
}
}

int main() {
    NavMeshRegistry reg;
    auto blob = make16PolyFlatSquare();
    NavMeshRef ref = reg.load(bytes(blob));
    CHECK(reg.isValid(ref));

    PathQueryService svc(reg);

    PathRequest req;
    req.mesh = ref;
    req.start = Vec3{0.5f, 0.0f, 0.5f};   // poly 0 centroid.
    req.goal  = Vec3{3.5f, 0.0f, 0.5f};   // poly 3 centroid.
    req.allowPartial = false;

    PathId id = svc.request(req);
    CHECK(id != 0);

    auto res = svc.wait(id, std::chrono::seconds{5});
    CHECK(res.has_value());
    if (!res) return gTestFailures;

    CHECK(res->success);
    CHECK(!res->partial);

    // A* walks all four polys end-to-end.
    CHECK_EQ(res->corridor.size(), std::size_t{4});
    CHECK(nearly(res->cost, 3.0f));

    // Funnel collapses the four polygon centroids to a straight line.
    CHECK_EQ(res->waypoints.size(), std::size_t{2});
    CHECK(nearly(res->waypoints.front().x, req.start.x));
    CHECK(nearly(res->waypoints.front().z, req.start.z));
    CHECK(nearly(res->waypoints.back().x, req.goal.x));
    CHECK(nearly(res->waypoints.back().z, req.goal.z));

    EXIT_WITH_RESULT();
}
