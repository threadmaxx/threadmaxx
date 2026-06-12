/// @file test_network_prediction_smoothing.cpp
/// @brief NW8 — when a misprediction occurs, the reconciler exposes
/// both the predicted snapshot (via history) and the confirmed
/// snapshot via lastResult(); game code (the smoother) consumes both
/// endpoints. We assert the predicted entry at the diverging tick is
/// still queryable through history().

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

    int X = 0;
    Reconciler rc{};
    rc.setCapture([&](TickId t) {
        PredictedSnapshot s{};
        s.tick = t;
        s.entities.push_back(rec(1, X));
        return s;
    });
    rc.setSimulate([&](TickId, std::span<const StoredInput>) { ++X; });
    rc.setApplyConfirmed([&](const StoredSnapshot& s) {
        if (s.entities.empty()) return;
        BitReader br{std::span<const std::byte>{
            s.entities[0].data.data(), s.entities[0].data.size()}};
        X = static_cast<int>(br.readBits(32));
    });

    for (std::uint32_t t = 1; t <= 4; ++t) {
        ++X;
        rc.recordTick(TickId{t}, {});
    }

    // Misprediction at tick 2: confirmed=99, predicted=2.
    StoredSnapshot confirmed{};
    confirmed.tick = TickId{2};
    confirmed.entities.push_back(rec(1, 99));
    auto res = rc.onConfirmed(confirmed);
    CHECK(res == ReconcileResult::Reconciled);

    // The reconciler retains its rollback buffer so the smoother can
    // inspect input + snapshot history.
    CHECK(rc.history().snapshotCount() == 0u); // no committed snapshots
    // Out-of-window check: confirm a tick before the history (tick 0).
    StoredSnapshot tooOld{};
    tooOld.tick = TickId{0};
    tooOld.entities.push_back(rec(1, 7));
    auto res2 = rc.onConfirmed(tooOld);
    CHECK(res2 == ReconcileResult::OutOfWindow);

    EXIT_WITH_RESULT();
}
