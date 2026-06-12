/// @file test_network_prediction_basic.cpp
/// @brief NW8 — client predicts forward; server delivers a matching
/// confirmation; result is Matched (zero reconciliation work).

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

} // namespace

int main() {
    using namespace threadmaxx::network;

    int gameStateX = 0;
    Reconciler r{};
    r.setCapture([&](TickId t) {
        PredictedSnapshot s{};
        s.tick = t;
        s.entities.push_back(rec(1, gameStateX));
        return s;
    });
    r.setSimulate([&](TickId, std::span<const StoredInput>) {
        ++gameStateX;
    });
    r.setApplyConfirmed([&](const StoredSnapshot& s) {
        // Restore X from confirmed.
        if (s.entities.empty()) return;
        BitReader br{std::span<const std::byte>{
            s.entities[0].data.data(), s.entities[0].data.size()}};
        gameStateX = static_cast<int>(br.readBits(32));
    });

    // Predict ticks 1..5.
    for (std::uint32_t t = 1; t <= 5; ++t) {
        ++gameStateX; // simulate
        r.recordTick(TickId{t}, {});
    }
    CHECK_EQ(r.predictedTick().value, 5u);

    // Server confirms tick 3 — matches our prediction at that tick
    // (which was gameStateX==3).
    StoredSnapshot confirmed{};
    confirmed.tick = TickId{3};
    confirmed.entities.push_back(rec(1, 3));
    auto res = r.onConfirmed(confirmed);
    CHECK(res == ReconcileResult::Matched);
    CHECK_EQ(r.confirmedTick().value, 3u);
    CHECK_EQ(r.reconcileCount(), 0u);

    EXIT_WITH_RESULT();
}
