/// @file test_network_delta_smoke.cpp
/// @brief NW6 — 100-entity scene where 10 entities mutate per tick;
/// delta packet size is dramatically smaller than the equivalent full
/// snapshot (target ≥5x smaller).

#include "Check.hpp"

#include <threadmaxx_network/delta.hpp>
#include <threadmaxx_network/replication.hpp>

#include <vector>

namespace {

threadmaxx::network::EntityRecord makeRec(std::uint64_t id, int v) {
    using namespace threadmaxx::network;
    EntityRecord r{};
    r.id = NetEntityId{id};
    BitWriter w;
    w.writeBits(static_cast<std::uint64_t>(v), 32);
    for (int i = 0; i < 24; ++i) w.writeBits(0xAAu, 8);
    const auto bytes = w.finish();
    r.data.assign(bytes.begin(), bytes.end());
    return r;
}

} // namespace

int main() {
    using namespace threadmaxx::network;

    std::vector<EntityRecord> baseline;
    baseline.reserve(100);
    for (std::uint64_t i = 1; i <= 100; ++i) {
        baseline.push_back(makeRec(i, 0));
    }
    // Full snapshot equivalent: ~ N * (8 + varuint + 28 bytes payload).
    std::size_t fullBytes = 0;
    for (const auto& r : baseline) {
        fullBytes += 8 /*id*/ + 1 /*varuint*/ + r.data.size();
    }

    // Delta with 10 entities changed.
    auto current = baseline;
    for (int i = 0; i < 10; ++i) current[i] = makeRec(static_cast<std::uint64_t>(i + 1), 9);

    DeltaEncoder enc;
    enc.setBaseline(TickId{1}, baseline);
    std::uint32_t seq = 0;
    auto packet = enc.produceDelta(SessionId{1}, TickId{2},
        std::span<const EntityRecord>{current.data(), current.size()}, seq);
    CHECK_EQ(enc.lastChangedCount(), 10u);
    CHECK_EQ(enc.lastDespawnCount(), 0u);

    // Delta should be ≥5x smaller than full.
    const std::size_t deltaBytes = packet.size();
    CHECK(deltaBytes * 5u <= fullBytes);

    EXIT_WITH_RESULT();
}
