/// @file test_network_commit_hash_mismatch.cpp
/// @brief NW10 — inject a divergence; DesyncReport fires with the
/// diverging tick + both hashes.

#include "Check.hpp"

#include <threadmaxx_network/diagnostics.hpp>

int main() {
    using namespace threadmaxx::network;

    SyncTracker tracker{};
    int desyncs = 0;
    DesyncReport caught{};
    tracker.onDesync([&](const DesyncReport& r) {
        ++desyncs;
        caught = r;
    });

    tracker.recordLocal(TickId{10}, 0x1111ull);
    tracker.recordLocal(TickId{11}, 0x2222ull);
    tracker.recordLocal(TickId{12}, 0x3333ull);

    // Matching remotes for 10 + 12; diverging for 11.
    tracker.recordRemote(TickId{10}, 0x1111ull);
    tracker.recordRemote(TickId{11}, 0xFFFFull);
    tracker.recordRemote(TickId{12}, 0x3333ull);

    CHECK_EQ(desyncs, 1);
    CHECK_EQ(caught.tick.value, 11u);
    CHECK_EQ(caught.localHash, 0x2222ull);
    CHECK_EQ(caught.remoteHash, 0xFFFFull);
    CHECK_EQ(tracker.desyncCount(), 1u);

    // A remote for a tick we don't know about is silently ignored.
    tracker.recordRemote(TickId{99}, 0xBEEFull);
    CHECK_EQ(desyncs, 1);

    EXIT_WITH_RESULT();
}
