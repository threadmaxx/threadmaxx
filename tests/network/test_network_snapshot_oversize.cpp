/// @file test_network_snapshot_oversize.cpp
/// @brief NW5 — a snapshot bigger than one fragment splits cleanly
/// and reassembles correctly.

#include "Check.hpp"

#include <threadmaxx_network/replication.hpp>

int main() {
    using namespace threadmaxx::network;

    SnapshotEncoder enc;
    enc.begin(TickId{1});
    // Write ~50 KB of payload — well over the fragment size.
    for (std::uint64_t i = 1; i <= 200; ++i) {
        enc.addEntity(NetEntityId{i}, [](NetEntityId, BitWriter& w) {
            for (int j = 0; j < 64; ++j) {
                w.writeBits(0xABCDEFu, 24);
            }
        });
    }
    std::uint32_t seq = 0;
    auto frags = enc.finishFragments(SessionId{1}, seq);
    CHECK(frags.size() > 1u); // confirms we actually fragmented

    SnapshotDecoder dec;
    int decodedEntities = 0;
    bool completed = false;
    for (const auto& f : frags) {
        if (dec.feed(std::span<const std::byte>{f.data(), f.size()},
                     [&decodedEntities](NetEntityId, BitReader& br) {
                         for (int j = 0; j < 64; ++j) {
                             CHECK_EQ(br.readBits(24), 0xABCDEFull);
                         }
                         ++decodedEntities;
                     })) {
            completed = true;
        }
    }
    CHECK(completed);
    CHECK_EQ(decodedEntities, 200);

    EXIT_WITH_RESULT();
}
