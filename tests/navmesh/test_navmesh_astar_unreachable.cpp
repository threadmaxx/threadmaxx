#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/query.hpp"
#include "threadmaxx_navmesh/threadmaxx_navmesh.hpp"

#include <chrono>

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

int main() {
    NavMeshRegistry reg;
    auto blob = makeTwoDisconnectedTiles();
    NavMeshRef ref = reg.load(bytes(blob));
    CHECK(reg.isValid(ref));
    CHECK(reg.lastLoadError() == NavMeshLoadError::None);

    PathQueryService svc(reg);

    // Start sits in T0 poly (0,0); goal sits inside T1 — there is no
    // portal between the two tiles so the goal is unreachable.
    PathRequest req;
    req.mesh = ref;
    req.start = Vec3{0.5f, 0.0f, 0.5f};
    req.goal  = Vec3{10.5f, 0.0f, 0.5f};
    req.allowPartial = false;

    PathId id = svc.request(req);
    CHECK(id != 0);
    CHECK(svc.lastRequestStatus() == PathRequestStatus::Accepted);

    auto res = svc.wait(id, std::chrono::seconds{5});
    CHECK(res.has_value());
    if (!res) return gTestFailures;

    CHECK(res->ready);
    CHECK(!res->success);
    CHECK(!res->partial);
    CHECK(res->corridor.empty());
    CHECK(res->waypoints.empty());

    // Goal off the mesh should fail pre-solve, not partial-succeed.
    PathRequest offReq = req;
    offReq.goal = Vec3{42.0f, 0.0f, 42.0f};
    PathId offId = svc.request(offReq);
    CHECK_EQ(offId, PathId{0});
    CHECK(svc.lastRequestStatus() == PathRequestStatus::GoalNotOnMesh);

    // Start off the mesh — same pre-solve failure path.
    PathRequest startOff = req;
    startOff.start = Vec3{-50.0f, 0.0f, -50.0f};
    PathId startId = svc.request(startOff);
    CHECK_EQ(startId, PathId{0});
    CHECK(svc.lastRequestStatus() == PathRequestStatus::StartNotOnMesh);

    // Invalid mesh ref.
    PathRequest badMesh = req;
    badMesh.mesh = NavMeshRef{};
    PathId badId = svc.request(badMesh);
    CHECK_EQ(badId, PathId{0});
    CHECK(svc.lastRequestStatus() == PathRequestStatus::InvalidMesh);

    EXIT_WITH_RESULT();
}
