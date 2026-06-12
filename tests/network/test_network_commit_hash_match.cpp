/// @file test_network_commit_hash_match.cpp
/// @brief NW10 — server + clients running the same deterministic
/// scenario report matching commitHash per tick; no DesyncReport.

#include "Check.hpp"

#include <threadmaxx_network/diagnostics.hpp>

int main() {
    using namespace threadmaxx::network;

    SyncTracker tracker{};
    int desyncs = 0;
    tracker.onDesync([&](const DesyncReport&) { ++desyncs; });

    // Local record then remote arrival, all matching.
    for (std::uint32_t t = 1; t <= 20; ++t) {
        const std::uint64_t h = 0xDEADBEEFull + t;
        tracker.recordLocal(TickId{t}, h);
        tracker.recordRemote(TickId{t}, h);
    }
    CHECK_EQ(desyncs, 0);
    CHECK_EQ(tracker.desyncCount(), 0u);
    CHECK_EQ(tracker.historyCount(), 20u);

    EXIT_WITH_RESULT();
}
