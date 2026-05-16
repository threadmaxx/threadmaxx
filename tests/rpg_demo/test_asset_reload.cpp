// §3.11.7 batch D7 — AssetReloaded event-flow regression test.
//
// Verifies that pressing F12 → injecting `kEdgeReloadShader` results
// in an `AssetReloaded` event being delivered to subscribers. The
// HudSystem's subscriber prints a log line; this test installs its
// own subscriber and counts deliveries.
//
// The renderer-side pipeline rebuild on `AssetReloaded` is deferred
// to a future `batch 9b`; this test confirms the event channel
// plumbing is wired correctly from the demo side.

#include "DemoTestHarness.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/Resource.hpp>

#include <atomic>
#include <cstdio>

int main() {
    using namespace rpg;
    using namespace rpg::testing;
    using namespace threadmaxx;

    auto fx = makeHeadless();

    // Local subscriber: tallies deliveries. Outlives the engine via
    // the RAII Subscription handle.
    std::atomic<int> deliveries{0};
    auto sub = fx.engine->events<AssetReloaded>().subscribeScoped(
        [&deliveries](const AssetReloaded&) {
            deliveries.fetch_add(1, std::memory_order_relaxed);
        });

    // Inject three F12 presses across three ticks.
    for (int i = 0; i < 3; ++i) {
        resetEdges();
        injectEdge(kEdgeReloadShader);
        fx.engine->step();
    }
    // Drain the event channel — events emitted during tick N are
    // delivered during tick N+1's tick-boundary drain. Step once
    // more so the last emit propagates.
    fx.engine->step();

    std::printf("[test_asset_reload] deliveries=%d\n",
                deliveries.load(std::memory_order_relaxed));
    // Each F12 press triggers exactly one AssetReloaded emit.
    CHECK_EQ(deliveries.load(), 3);

    EXIT_WITH_RESULT();
}
