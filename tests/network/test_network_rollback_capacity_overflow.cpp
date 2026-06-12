/// @file test_network_rollback_capacity_overflow.cpp
/// @brief NW7 — exceeding historyTicks drops the oldest entry FIFO.

#include "Check.hpp"

#include <threadmaxx_network/rollback.hpp>

int main() {
    using namespace threadmaxx::network;

    RollbackConfig cfg{};
    cfg.historyTicks = 5;
    cfg.keepEventHistory = true;
    RollbackBuffer rb{cfg};

    for (std::uint32_t t = 1; t <= 10; ++t) {
        StoredInput i{};   i.peer = PeerId{1}; i.tick = TickId{t};
        StoredSnapshot s{}; s.tick = TickId{t};
        StoredEvent e{};   e.tick = TickId{t}; e.bytes = {std::byte{0x1}};
        rb.pushInput(i);
        rb.pushSnapshot(s);
        rb.pushEvent(e);
    }
    CHECK_EQ(rb.inputCount(), 5u);
    CHECK_EQ(rb.snapshotCount(), 5u);
    CHECK_EQ(rb.eventCount(), 5u);

    // Oldest survivors are tick 6.
    CHECK(rb.inputs().front().tick.value == 6u);
    CHECK(rb.snapshots().front().tick.value == 6u);
    CHECK(rb.events().front().tick.value == 6u);

    // Disabling event history yields zero events.
    RollbackConfig cfg2 = cfg;
    cfg2.keepEventHistory = false;
    RollbackBuffer rb2{cfg2};
    StoredEvent e{};
    e.tick = TickId{1};
    rb2.pushEvent(e);
    CHECK_EQ(rb2.eventCount(), 0u);

    EXIT_WITH_RESULT();
}
