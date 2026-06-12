/// @file test_network_prediction_misprediction.cpp
/// @brief NW8 — predicted state diverged from server's; reconcile
/// rewinds to confirmed tick and replays with stored inputs.

#include "Check.hpp"

#include <threadmaxx_network/prediction.hpp>

namespace {

threadmaxx::network::EntityRecord rec(std::uint64_t id, int v) {
    using namespace threadmaxx::network;
    EntityRecord r{};
    r.id = NetEntityId{id};
    BitWriter w; w.writeBits(static_cast<std::uint64_t>(v), 32);
    const auto out = w.finish();
    r.data.assign(out.begin(), out.end());
    return r;
}

int decodeX(const threadmaxx::network::EntityRecord& r) {
    using namespace threadmaxx::network;
    BitReader br{std::span<const std::byte>{r.data.data(), r.data.size()}};
    return static_cast<int>(br.readBits(32));
}

} // namespace

int main() {
    using namespace threadmaxx::network;

    // Game-side state: one int, X.
    int X = 0;
    Reconciler rc{};
    rc.setCapture([&](TickId t) {
        PredictedSnapshot s{};
        s.tick = t;
        s.entities.push_back(rec(1, X));
        return s;
    });
    rc.setSimulate([&](TickId, std::span<const StoredInput> inputs) {
        // Inputs interpret as: add the first byte to X.
        int add = 1;
        if (!inputs.empty() && !inputs[0].bytes.empty()) {
            add = static_cast<int>(inputs[0].bytes[0]);
        }
        X += add;
    });
    rc.setApplyConfirmed([&](const StoredSnapshot& s) {
        X = decodeX(s.entities.front());
    });

    // Simulate 5 ticks with input=+1 each.
    for (std::uint32_t t = 1; t <= 5; ++t) {
        StoredInput in{};
        in.peer = PeerId{1};
        in.tick = TickId{t};
        in.bytes = {std::byte{1}};
        X += 1;                              // local simulate
        rc.recordTick(TickId{t}, {&in, 1});
    }
    CHECK_EQ(X, 5);

    // Server says tick 2 was actually X=99 (we predicted X=2).
    StoredSnapshot confirmed{};
    confirmed.tick = TickId{2};
    confirmed.entities.push_back(rec(1, 99));
    auto r = rc.onConfirmed(confirmed);
    CHECK(r == ReconcileResult::Reconciled);
    CHECK_EQ(rc.reconcileCount(), 1u);

    // After replay from tick 3..5 with +1 each: X = 99 + 1 + 1 + 1 = 102.
    CHECK_EQ(X, 102);

    EXIT_WITH_RESULT();
}
