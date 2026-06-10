#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/crowd.hpp"
#include "threadmaxx_navmesh/query.hpp"

#include <cmath>
#include <cstddef>
#include <vector>

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

// N6 — `BatchPathSolver::solve` returns the same per-request result a
// freshly-built synchronous `PathQueryService` would have produced. The
// reference service is constructed inside this test so it shares no
// state with the batch solver: matching outputs prove the shared solver
// internals (detail/solver.hpp) behave identically across both paths.

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

    // 64 (start, goal) pairs fanning across the 4x4 grid. Pattern lines
    // up with the N5 async smoke test so the reference behavior matches.
    constexpr std::size_t kCount = 64;
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

    // Reference path: synchronous single-request service.
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

    // Batch solve via 2 workers — exercises the worker-pool path.
    BatchPathSolver solver(reg, BatchPathSolver::Config{2});
    CHECK_EQ(solver.workerCount(), std::uint32_t{2});
    BatchPathRequest batch;
    batch.requests = reqs;
    BatchPathResult out = solver.solve(batch);
    CHECK_EQ(out.results.size(), kCount);

    for (std::size_t i = 0; i < kCount; ++i) {
        const PathResult& got = out.results[i];
        const PathResult& exp = reference[i];
        CHECK(got.ready);
        CHECK_EQ(got.success, exp.success);
        CHECK_EQ(got.partial, exp.partial);
        CHECK_EQ(got.corridor.size(), exp.corridor.size());
        CHECK_EQ(got.waypoints.size(), exp.waypoints.size());
        CHECK(nearly(got.cost, exp.cost));
        for (std::size_t k = 0; k < got.waypoints.size() &&
                                k < exp.waypoints.size(); ++k) {
            CHECK(nearly(got.waypoints[k].x, exp.waypoints[k].x));
            CHECK(nearly(got.waypoints[k].z, exp.waypoints[k].z));
        }
        for (std::size_t k = 0; k < got.corridor.size() &&
                                k < exp.corridor.size(); ++k) {
            CHECK_EQ(got.corridor[k].tileId, exp.corridor[k].tileId);
            CHECK_EQ(got.corridor[k].polyId, exp.corridor[k].polyId);
        }
    }

    // Empty batch is a fast-path no-op.
    BatchPathResult empty = solver.solve(BatchPathRequest{});
    CHECK_EQ(empty.results.size(), std::size_t{0});

    // Synchronous mode (workerThreads == 0) is also part of the
    // contract — produces matching results without spawning threads.
    {
        BatchPathSolver syncSolver(reg, BatchPathSolver::Config{0});
        CHECK_EQ(syncSolver.workerCount(), std::uint32_t{0});
        BatchPathResult syncOut = syncSolver.solve(batch);
        CHECK_EQ(syncOut.results.size(), kCount);
        for (std::size_t i = 0; i < kCount; ++i) {
            CHECK_EQ(syncOut.results[i].success, reference[i].success);
            CHECK_EQ(syncOut.results[i].corridor.size(),
                     reference[i].corridor.size());
            CHECK(nearly(syncOut.results[i].cost, reference[i].cost));
        }
    }

    EXIT_WITH_RESULT();
}
