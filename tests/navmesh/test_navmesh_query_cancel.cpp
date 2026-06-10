#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/query.hpp"

#include <chrono>
#include <cstddef>
#include <vector>

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

// N5 — cancellation. Two distinct cases need to hold:
//
//   1. Cancel a request that's been queued but not yet solved. The
//      worker must skip the solve on pop and `wait()` must return
//      nullopt without crashing.
//   2. Cancel a request that's already completed. tryGet() must drop
//      the stored result.
//
// We exercise the pending case by queueing many requests and cancelling
// the LAST one immediately — the worker can't have started it yet
// because it's busy with the earlier requests. The cancelled-after-ready
// case is exercised after waiting for the corresponding result.

int main() {
    NavMeshRegistry reg;
    auto blob = make16PolyFlatSquare();
    NavMeshRef ref = reg.load(bytes(blob));
    CHECK(reg.isValid(ref));

    PathQueryService svc(reg, PathQueryService::Config{1});

    PathRequest base;
    base.mesh = ref;
    base.start = Vec3{0.5f, 0.0f, 0.5f};
    base.goal  = Vec3{3.5f, 0.0f, 3.5f};
    base.allowPartial = false;

    // Pre-cancel a freshly-issued id. Worker hasn't seen it yet because
    // nothing else is queued, so the race is genuine — but the test
    // tolerates either pop-then-skip OR cancelled-after-store. The
    // contract is that wait() returns nullopt.
    {
        PathId id = svc.request(base);
        CHECK(id != 0);
        svc.cancel(id);
        auto r = svc.wait(id, std::chrono::milliseconds{200});
        CHECK(!r.has_value());
    }

    // Queue ~64 requests so the last entry is virtually guaranteed to be
    // still pending when we cancel it.
    constexpr std::size_t kCount = 64;
    std::vector<PathId> ids;
    ids.reserve(kCount);
    for (std::size_t i = 0; i < kCount; ++i) {
        PathId id = svc.request(base);
        CHECK(id != 0);
        ids.push_back(id);
    }
    const PathId tail = ids.back();
    svc.cancel(tail);

    // The cancelled id never produces a result.
    auto r = svc.wait(tail, std::chrono::milliseconds{500});
    CHECK(!r.has_value());

    // The other 63 still resolve normally.
    for (std::size_t i = 0; i < kCount - 1; ++i) {
        auto res = svc.wait(ids[i], std::chrono::seconds{10});
        CHECK(res.has_value());
        if (res) CHECK(res->success);
    }

    // Cancel a stored (ready) result — tryGet must reflect the drop.
    {
        PathId id = svc.request(base);
        CHECK(id != 0);
        auto res = svc.wait(id, std::chrono::seconds{5});
        CHECK(res.has_value());
        svc.cancel(id);
        CHECK(!svc.tryGet(id).has_value());
    }

    // Cancelling an unknown id is a no-op and doesn't crash.
    svc.cancel(0xCAFEF00D);

    EXIT_WITH_RESULT();
}
