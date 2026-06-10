#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/query.hpp"

#include <chrono>
#include <cstddef>
#include <thread>
#include <vector>

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

// N5 — `clear()` drops every queued, in-flight, and stored request.
// The contract is that after `clear()` returns, `tryGet()` for any of
// the previously-issued ids returns nullopt, `pendingCount` is 0, and
// `storedCount` is 0. In-flight solves may still complete; their result
// must be discarded rather than stored.

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

    // Queue 64 requests, then clear() while many are still pending.
    constexpr std::size_t kCount = 64;
    std::vector<PathId> ids;
    ids.reserve(kCount);
    for (std::size_t i = 0; i < kCount; ++i) {
        PathId id = svc.request(base);
        CHECK(id != 0);
        ids.push_back(id);
    }
    svc.clear();

    // Pending queue drained.
    CHECK_EQ(svc.pendingCount(), std::size_t{0});

    // Give any in-flight solve a chance to wrap up and try to store a
    // result. Whatever was happening must NOT leak past the clear().
    std::this_thread::sleep_for(std::chrono::milliseconds{50});

    CHECK_EQ(svc.storedCount(), std::size_t{0});
    for (PathId id : ids) {
        CHECK(!svc.tryGet(id).has_value());
        // wait() must return nullopt promptly because the id is now
        // tombstoned.
        auto r = svc.wait(id, std::chrono::milliseconds{100});
        CHECK(!r.has_value());
    }

    // The service remains usable after clear() — fresh requests resolve
    // normally with brand-new ids.
    PathId fresh = svc.request(base);
    CHECK(fresh != 0);
    auto freshRes = svc.wait(fresh, std::chrono::seconds{5});
    CHECK(freshRes.has_value());
    if (freshRes) CHECK(freshRes->success);

    // clear() on an empty service is a no-op.
    svc.clear();
    CHECK_EQ(svc.storedCount(), std::size_t{0});
    CHECK_EQ(svc.pendingCount(), std::size_t{0});

    EXIT_WITH_RESULT();
}
