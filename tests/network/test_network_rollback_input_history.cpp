/// @file test_network_rollback_input_history.cpp
/// @brief NW7 — 120 ticks of input retained; lookup by tick works.

#include "Check.hpp"

#include <threadmaxx_network/rollback.hpp>

int main() {
    using namespace threadmaxx::network;

    RollbackConfig cfg{};
    cfg.historyTicks = 120;
    RollbackBuffer rb{cfg};

    for (std::uint32_t t = 1; t <= 120; ++t) {
        StoredInput in{};
        in.peer = PeerId{1};
        in.tick = TickId{t};
        in.bytes = {std::byte{static_cast<unsigned char>(t & 0xFF)}};
        rb.pushInput(in);
    }
    CHECK_EQ(rb.inputCount(), 120u);

    auto rows = rb.inputsForTick(TickId{60});
    CHECK_EQ(rows.size(), 1u);
    CHECK_EQ(rows[0].bytes.size(), 1u);
    CHECK_EQ(static_cast<int>(rows[0].bytes[0]), 60);

    // Pushing a 121st tick drops the oldest (tick 1).
    StoredInput overflow{};
    overflow.peer = PeerId{1};
    overflow.tick = TickId{121};
    overflow.bytes = {std::byte{0xFF}};
    rb.pushInput(overflow);
    CHECK_EQ(rb.inputCount(), 120u);
    CHECK(rb.inputsForTick(TickId{1}).empty());
    CHECK(!rb.inputsForTick(TickId{121}).empty());

    EXIT_WITH_RESULT();
}
