#include "../Check.hpp"

#include "fixtures/blob_builder.hpp"

#include "threadmaxx_navmesh/threadmaxx_navmesh.hpp"

using namespace threadmaxx::navmesh;
using namespace threadmaxx::navmesh::test_fixtures;

int main() {
    NavMeshRegistry reg;
    auto blob = make16PolyFlatSquare();

    // Load → unload → reload. After unload, the original ref must
    // observe as stale; the reloaded asset gets a fresh generation
    // even though it occupies the same slot.
    NavMeshRef ref1 = reg.load(bytes(blob));
    CHECK(reg.isValid(ref1));
    const auto gen1 = ref1.generation;

    reg.unload(ref1);
    CHECK(!reg.isValid(ref1));
    CHECK_EQ(reg.size(), std::size_t{0});
    CHECK(reg.find(ref1) == nullptr);
    CHECK(!reg.meta(ref1).has_value());

    NavMeshRef ref2 = reg.load(bytes(blob));
    CHECK(reg.isValid(ref2));
    CHECK_EQ(reg.size(), std::size_t{1});
    // Same slot (id) but generation bumped → stale ref1 stays invalid.
    CHECK_EQ(ref2.id, ref1.id);
    CHECK(ref2.generation != gen1);
    CHECK(!reg.isValid(ref1));

    // Double-unload is a no-op, not a crash.
    reg.unload(ref1);
    reg.unload(NavMeshRef{});  // unloading an invalid ref is also OK.
    CHECK(reg.isValid(ref2));

    // Two simultaneous loads → two valid refs, both meta-queryable.
    NavMeshRef ref3 = reg.load(bytes(blob));
    CHECK(reg.isValid(ref2));
    CHECK(reg.isValid(ref3));
    CHECK(ref2.id != ref3.id);
    CHECK_EQ(reg.size(), std::size_t{2});

    EXIT_WITH_RESULT();
}
