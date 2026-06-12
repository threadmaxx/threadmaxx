/// @file test_network_rollback_snapshot_history.cpp
/// @brief NW7 — snapshot history retains by tick and supports random
/// access lookup.

#include "Check.hpp"

#include <threadmaxx_network/rollback.hpp>

int main() {
    using namespace threadmaxx::network;

    RollbackBuffer rb{};
    for (std::uint32_t t = 1; t <= 50; ++t) {
        StoredSnapshot s{};
        s.tick = TickId{t};
        s.commitHash = 0xC0FFEE0000ull + t;
        rb.pushSnapshot(s);
    }
    CHECK_EQ(rb.snapshotCount(), 50u);

    const auto* ten = rb.snapshotAt(TickId{10});
    CHECK(ten != nullptr);
    CHECK_EQ(ten->commitHash, 0xC0FFEE0000ull + 10u);

    // Out-of-order insert lands in sorted position.
    StoredSnapshot late{};
    late.tick = TickId{25};
    late.commitHash = 0xDEADull;
    rb.pushSnapshot(late);
    CHECK_EQ(rb.snapshotAt(TickId{25})->commitHash, 0xDEADull);

    // Compact before tick 30 → 20 entries remain (30..50; 25 already
    // dropped).
    rb.compactBefore(TickId{30});
    CHECK_EQ(rb.snapshotCount(), 21u);
    CHECK(rb.snapshotAt(TickId{1}) == nullptr);
    CHECK(rb.snapshotAt(TickId{30}) != nullptr);

    EXIT_WITH_RESULT();
}
