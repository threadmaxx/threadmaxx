/// @file test_network_snapshot_roundtrip.cpp
/// @brief NW5 — server encodes a 100-entity snapshot; client decodes
/// and recovers every entity with the same payload bytes.

#include "Check.hpp"

#include <threadmaxx_network/replication.hpp>

#include <array>
#include <unordered_map>

int main() {
    using namespace threadmaxx::network;

    SnapshotEncoder enc;
    enc.begin(TickId{1234});
    for (std::uint64_t i = 1; i <= 100; ++i) {
        enc.addEntity(NetEntityId{i}, [i](NetEntityId,
                                           BitWriter& w) {
            w.writeVarUInt(i * 7u);
            w.writeBits(0xAA55u, 16);
        });
    }
    std::uint32_t seq = 0;
    auto frags = enc.finishFragments(SessionId{42}, seq);
    CHECK(!frags.empty());

    SnapshotDecoder dec;
    std::unordered_map<std::uint64_t, std::uint64_t> seen;
    bool completed = false;
    for (const auto& f : frags) {
        if (dec.feed(std::span<const std::byte>{f.data(), f.size()},
                     [&seen](NetEntityId id, BitReader& br) {
                         std::uint64_t v = br.readVarUInt();
                         std::uint64_t tag = br.readBits(16);
                         CHECK_EQ(tag, 0xAA55ull);
                         seen[id.value] = v;
                     })) {
            completed = true;
        }
    }
    CHECK(completed);
    CHECK_EQ(seen.size(), 100u);
    for (std::uint64_t i = 1; i <= 100; ++i) {
        auto it = seen.find(i);
        CHECK(it != seen.end());
        CHECK_EQ(it->second, i * 7u);
    }
    CHECK_EQ(dec.lastCompletedTick().value, 1234u);

    EXIT_WITH_RESULT();
}
