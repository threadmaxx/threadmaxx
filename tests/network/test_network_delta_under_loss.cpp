/// @file test_network_delta_under_loss.cpp
/// @brief NW6 — when a delta arrives whose baseline tick doesn't match
/// the decoder's, `feed` refuses it (game code is expected to request
/// a full-snapshot resync). When the baseline matches the chain
/// resumes cleanly.

#include "Check.hpp"

#include <threadmaxx_network/delta.hpp>

#include <vector>

namespace {

threadmaxx::network::EntityRecord rec(std::uint64_t id, int v) {
    using namespace threadmaxx::network;
    EntityRecord r{};
    r.id = NetEntityId{id};
    BitWriter w;
    w.writeBits(static_cast<std::uint64_t>(v), 32);
    const auto out = w.finish();
    r.data.assign(out.begin(), out.end());
    return r;
}

} // namespace

int main() {
    using namespace threadmaxx::network;

    std::vector<EntityRecord> baseline = {rec(1, 10), rec(2, 20)};

    DeltaEncoder enc;
    enc.setBaseline(TickId{0}, baseline);

    std::vector<EntityRecord> current = {rec(1, 11), rec(2, 20)};
    std::uint32_t seq = 0;
    auto pkt = enc.produceDelta(SessionId{1}, TickId{1},
        std::span<const EntityRecord>{current.data(), current.size()}, seq);

    // Decoder believes its baseline is tick 99 (no matching snapshot).
    DeltaDecoder dec;
    dec.setBaseline(TickId{99}, baseline);
    int hits = 0;
    CHECK(!dec.feed(std::span<const std::byte>{pkt.data(), pkt.size()},
                    [&](NetEntityId, BitReader&) { ++hits; }));
    CHECK_EQ(hits, 0);

    // Now reset decoder baseline to the matching tick and re-feed.
    dec.setBaseline(TickId{0}, baseline);
    CHECK(dec.feed(std::span<const std::byte>{pkt.data(), pkt.size()},
                   [&](NetEntityId, BitReader&) { ++hits; }));
    CHECK_EQ(hits, 1);
    CHECK_EQ(dec.lastAppliedTick().value, 1u);

    EXIT_WITH_RESULT();
}
