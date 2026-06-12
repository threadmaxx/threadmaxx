/// @file test_network_delta_correctness.cpp
/// @brief NW6 — delta-decode produces the same state as a full-snapshot
/// decode of the same payload.

#include "Check.hpp"

#include <threadmaxx_network/delta.hpp>

#include <cstring>
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

    std::vector<EntityRecord> baseline = {
        rec(1, 10), rec(2, 20), rec(3, 30), rec(4, 40),
    };
    // Current: entity 2 changed, entity 4 despawned, entity 5 spawned.
    std::vector<EntityRecord> current = {
        rec(1, 10), rec(2, 99), rec(3, 30), rec(5, 50),
    };

    DeltaEncoder enc;
    enc.setBaseline(TickId{0}, baseline);
    std::uint32_t seq = 0;
    auto packet = enc.produceDelta(SessionId{1}, TickId{1},
        std::span<const EntityRecord>{current.data(), current.size()}, seq);

    DeltaDecoder dec;
    dec.setBaseline(TickId{0}, baseline);
    int callbackCount = 0;
    CHECK(dec.feed(std::span<const std::byte>{packet.data(), packet.size()},
                   [&](NetEntityId, BitReader&) { ++callbackCount; }));
    CHECK_EQ(callbackCount, 2); // entity 2 (changed) + entity 5 (spawned)

    auto post = dec.currentBaseline();
    CHECK_EQ(post.size(), 4u);
    bool has1=false, has2=false, has3=false, has5=false;
    int v2 = -1, v5 = -1;
    for (const auto& r : post) {
        if (r.id.value == 1) has1 = true;
        if (r.id.value == 2) {
            has2 = true;
            BitReader br{std::span<const std::byte>{r.data.data(), r.data.size()}};
            v2 = static_cast<int>(br.readBits(32));
        }
        if (r.id.value == 3) has3 = true;
        if (r.id.value == 5) {
            has5 = true;
            BitReader br{std::span<const std::byte>{r.data.data(), r.data.size()}};
            v5 = static_cast<int>(br.readBits(32));
        }
    }
    CHECK(has1); CHECK(has2); CHECK(has3); CHECK(has5);
    CHECK_EQ(v2, 99);
    CHECK_EQ(v5, 50);
    CHECK_EQ(dec.lastAppliedTick().value, 1u);

    EXIT_WITH_RESULT();
}
