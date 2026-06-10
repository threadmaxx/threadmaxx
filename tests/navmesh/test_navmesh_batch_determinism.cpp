#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/crowd.hpp"

#include <cstddef>
#include <vector>

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

// N6 — solving the same batch twice (against the same registry, same
// worker count) must produce byte-identical results. Determinism is
// structural: each entry is solved independently in its own scratch,
// the result vector is written by index, and `solvePrepared` is pure
// over `(mesh, request)`. The test exercises both a same-instance
// resolve (re-use the solver) and a fresh-instance resolve (new pool).

namespace {

bool sameResult(const PathResult& a, const PathResult& b) {
    if (a.ready != b.ready) return false;
    if (a.success != b.success) return false;
    if (a.partial != b.partial) return false;
    if (a.cost != b.cost) return false;
    if (a.corridor.size() != b.corridor.size()) return false;
    if (a.waypoints.size() != b.waypoints.size()) return false;
    for (std::size_t i = 0; i < a.corridor.size(); ++i) {
        if (a.corridor[i].tileId != b.corridor[i].tileId) return false;
        if (a.corridor[i].polyId != b.corridor[i].polyId) return false;
    }
    for (std::size_t i = 0; i < a.waypoints.size(); ++i) {
        const Vec3& va = a.waypoints[i];
        const Vec3& vb = b.waypoints[i];
        if (va.x != vb.x || va.y != vb.y || va.z != vb.z) return false;
    }
    return true;
}

}

int main() {
    NavMeshRegistry reg;
    auto blob = make16PolyFlatSquare();
    NavMeshRef ref = reg.load(bytes(blob));
    CHECK(reg.isValid(ref));

    constexpr std::size_t kCount = 64;
    BatchPathRequest batch;
    batch.requests.reserve(kCount);
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
        batch.requests.push_back(r);
    }

    // Same-instance back-to-back solves must match byte-for-byte. The
    // pool is reused, so this also validates that worker scratch is
    // properly recycled between batches.
    BatchPathSolver solver(reg, BatchPathSolver::Config{2});
    BatchPathResult a = solver.solve(batch);
    BatchPathResult b = solver.solve(batch);
    CHECK_EQ(a.results.size(), kCount);
    CHECK_EQ(b.results.size(), kCount);
    for (std::size_t i = 0; i < kCount; ++i) {
        CHECK(sameResult(a.results[i], b.results[i]));
    }

    // Fresh-instance resolve — same pool size, brand-new threads. Still
    // bit-identical: the worker schedule doesn't affect per-request
    // output because each request is solved end-to-end on one worker.
    {
        BatchPathSolver fresh(reg, BatchPathSolver::Config{2});
        BatchPathResult c = fresh.solve(batch);
        CHECK_EQ(c.results.size(), kCount);
        for (std::size_t i = 0; i < kCount; ++i) {
            CHECK(sameResult(a.results[i], c.results[i]));
        }
    }

    // Different pool size — should also match. The shape of the work
    // schedule changes (which worker pulls which index) but the per-
    // index result is invariant under that change.
    {
        BatchPathSolver four(reg, BatchPathSolver::Config{4});
        BatchPathResult d = four.solve(batch);
        CHECK_EQ(d.results.size(), kCount);
        for (std::size_t i = 0; i < kCount; ++i) {
            CHECK(sameResult(a.results[i], d.results[i]));
        }
    }

    EXIT_WITH_RESULT();
}
