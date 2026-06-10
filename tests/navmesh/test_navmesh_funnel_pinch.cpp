#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/query.hpp"
#include "threadmaxx_navmesh/threadmaxx_navmesh.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

// N4 — funnel pinch around a forbidden polygon. The area-mask strip
// has poly 1 tagged area 1 (water). Masking out area 1 forces the
// corridor `0 → 3 → 4 → 5 → 2` around the bypass; the funnel pinches
// against the two inner-corner vertices (1, 0, 1) and (2, 0, 1) that
// touch poly 1's boundary.

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

    PathRequest req;
    req.mesh = ref;
    req.start = Vec3{0.5f, 0.0f, 0.5f};   // poly 0 centroid.
    req.goal  = Vec3{2.5f, 0.0f, 0.5f};   // poly 2 centroid.
    req.areaMask = ~(std::uint32_t{1} << 1);  // mask out water.
    req.allowPartial = false;

    PathId id = svc.request(req);
    CHECK(id != 0);

    auto res = svc.wait(id, std::chrono::seconds{5});
    CHECK(res.has_value());
    if (!res) return gTestFailures;

    CHECK(res->success);
    CHECK(!res->partial);

    // Confirm the detour corridor — five polys, cost 4.0.
    CHECK_EQ(res->corridor.size(), std::size_t{5});
    CHECK(nearly(res->cost, 4.0f));

    // Funnel pinches at the two inner corners of the bypass.
    CHECK_EQ(res->waypoints.size(), std::size_t{4});
    CHECK(nearly(res->waypoints.front().x, req.start.x));
    CHECK(nearly(res->waypoints.front().z, req.start.z));
    CHECK(nearly(res->waypoints[1].x, 1.0f));
    CHECK(nearly(res->waypoints[1].z, 1.0f));
    CHECK(nearly(res->waypoints[2].x, 2.0f));
    CHECK(nearly(res->waypoints[2].z, 1.0f));
    CHECK(nearly(res->waypoints.back().x, req.goal.x));
    CHECK(nearly(res->waypoints.back().z, req.goal.z));

    EXIT_WITH_RESULT();
}
