#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/query.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

// N5 — submit 100 requests through the async worker; each must produce
// the same result the synchronous (workerThreads == 0) path would have.
// The reference path runs against a freshly-constructed sync service so
// the two share no scratch / mutex / queue state.

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

    // 100 requests fanning out across the 4x4 grid. Each (sx, sz) -> (gx, gz)
    // pair is deterministic so the reference solve is reproducible.
    constexpr std::size_t kCount = 100;
    std::vector<PathRequest> reqs;
    reqs.reserve(kCount);
    for (std::size_t i = 0; i < kCount; ++i) {
        PathRequest r;
        r.mesh = ref;
        const float sx = 0.5f + static_cast<float>(i % 4u);
        const float sz = 0.5f + static_cast<float>((i / 4u) % 4u);
        const float gx = 3.5f - static_cast<float>(i % 4u);
        const float gz = 3.5f - static_cast<float>((i / 4u) % 4u);
        r.start = Vec3{sx, 0.0f, sz};
        r.goal  = Vec3{gx, 0.0f, gz};
        r.allowPartial = false;
        reqs.push_back(r);
    }

    // Reference (synchronous) path.
    std::vector<PathResult> reference;
    reference.reserve(kCount);
    {
        PathQueryService sync(reg, PathQueryService::Config{0});
        for (const auto& r : reqs) {
            PathId id = sync.request(r);
            CHECK(id != 0);
            auto res = sync.tryGet(id);
            CHECK(res.has_value());
            if (res) reference.push_back(*res);
        }
    }
    CHECK_EQ(reference.size(), kCount);

    // Async service: enqueue 100 requests, then wait on each in turn.
    PathQueryService svc(reg, PathQueryService::Config{1});
    CHECK_EQ(svc.workerCount(), std::uint32_t{1});

    std::vector<PathId> ids;
    ids.reserve(kCount);
    for (const auto& r : reqs) {
        PathId id = svc.request(r);
        CHECK(id != 0);
        ids.push_back(id);
    }

    // Spec says results match the synchronous path exactly. Use a
    // generous timeout to keep the test stable under load on CI.
    for (std::size_t i = 0; i < kCount; ++i) {
        auto res = svc.wait(ids[i], std::chrono::seconds{10});
        CHECK(res.has_value());
        if (!res) continue;
        const PathResult& ref_r = reference[i];
        CHECK_EQ(res->success, ref_r.success);
        CHECK_EQ(res->partial, ref_r.partial);
        CHECK_EQ(res->corridor.size(), ref_r.corridor.size());
        CHECK_EQ(res->waypoints.size(), ref_r.waypoints.size());
        CHECK(nearly(res->cost, ref_r.cost));
        for (std::size_t k = 0; k < res->waypoints.size() &&
                                k < ref_r.waypoints.size(); ++k) {
            CHECK(nearly(res->waypoints[k].x, ref_r.waypoints[k].x));
            CHECK(nearly(res->waypoints[k].z, ref_r.waypoints[k].z));
        }
        for (std::size_t k = 0; k < res->corridor.size() &&
                                k < ref_r.corridor.size(); ++k) {
            CHECK_EQ(res->corridor[k].tileId, ref_r.corridor[k].tileId);
            CHECK_EQ(res->corridor[k].polyId, ref_r.corridor[k].polyId);
        }
    }

    // After draining, pendingCount drops to 0 and storedCount matches.
    CHECK_EQ(svc.pendingCount(), std::size_t{0});
    CHECK_EQ(svc.storedCount(), kCount);

    EXIT_WITH_RESULT();
}
