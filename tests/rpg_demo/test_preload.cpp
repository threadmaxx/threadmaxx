// §3.11.7 batch D7 — boot-time preload regression test.
//
// Boots the demo via `makeHeadless` (which runs `engine.initialize`
// → `DemoGame::onSetup`, including the `preloadUntil` call).
// Verifies:
//   - `aggregateLoaderStats()` reports zero pending after init.
//   - `ready` is the expected `PreloadLoader::kAssetCount` (64).
//   - `memoryFootprint` is non-zero (loader reported it).
//
// The `preloadUntil` call inside onSetup pumps the loader's
// `update()` every cycle WITHOUT advancing the simulation, so a
// freshly-initialized engine has the loader fully drained.

#include "DemoTestHarness.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Resource.hpp>

#include <cstdio>

int main() {
    using namespace rpg;
    using namespace rpg::testing;
    using namespace threadmaxx;

    auto fx = makeHeadless();

    const auto s = fx.engine->aggregateLoaderStats();
    std::printf("[test_preload] pending=%llu inFlight=%llu ready=%llu "
                "memMiB=%.1f\n",
                static_cast<unsigned long long>(s.pendingLoads),
                static_cast<unsigned long long>(s.inFlight),
                static_cast<unsigned long long>(s.ready),
                static_cast<double>(s.memoryFootprint) /
                    (1024.0 * 1024.0));

    // `preloadUntil` should have drained the queue entirely.
    CHECK_EQ(s.pendingLoads, 0u);
    CHECK_EQ(s.inFlight,     0u);
    // 64 simulated assets reported as "ready".
    CHECK(s.ready >= 64u);
    // Each fake asset claimed 64 KiB → 64 × 64 KiB = 4 MiB.
    CHECK(s.memoryFootprint >= 4u * 1024u * 1024u);

    EXIT_WITH_RESULT();
}
