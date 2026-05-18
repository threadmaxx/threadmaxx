// §3.11.7 batch D7 — AssetReloaded event-flow regression test.
//
// Verifies that pressing F12 → injecting `kEdgeReloadShader` results
// in an `AssetReloaded` event being delivered to subscribers.
//
// §3.11 batch 9b.3: F12 no longer emits a synthetic AssetReloaded
// directly from HudSystem; it now invokes a `reloadShadersFn`
// callback. In production main.cpp installs a callback that forwards
// to `VulkanRenderer::reloadShaders` (which calls
// `markResourceStale<Shader>` → ShaderLoader.update reads disk →
// AssetReloaded). In this headless test we install a synthetic
// callback that emits AssetReloaded directly, preserving the prior
// "one F12 press → one AssetReloaded delivery" gate without
// requiring a real renderer.

#include "DemoTestHarness.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/Resource.hpp>

#include <atomic>
#include <cstdio>
#include <typeindex>

int main() {
    using namespace rpg;
    using namespace rpg::testing;
    using namespace threadmaxx;

    // Manual fixture: we need to install the reload callback BEFORE
    // engine.initialize fires (HudSystem captures the callback by
    // copy at its ctor, which runs inside `DemoGame::onSetup` →
    // `engine.initialize`).
    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 2;
    auto game   = std::make_unique<rpg::DemoGame>();
    auto engine = std::make_unique<Engine>(cfg);

    game->setReloadShadersFn([eng = engine.get()] {
        eng->events<AssetReloaded>().emit(
            AssetReloaded{0, 0, 1, 1, std::type_index(typeid(void))});
    });

    CHECK(engine->initialize(*game));

    // Local subscriber: tallies deliveries. Outlives the engine via
    // the RAII Subscription handle.
    std::atomic<int> deliveries{0};
    auto sub = engine->events<AssetReloaded>().subscribeScoped(
        [&deliveries](const AssetReloaded&) {
            deliveries.fetch_add(1, std::memory_order_relaxed);
        });

    // Inject three F12 presses across three ticks.
    for (int i = 0; i < 3; ++i) {
        resetEdges();
        injectEdge(kEdgeReloadShader);
        engine->step();
    }
    // Drain the event channel — events emitted during tick N are
    // delivered during tick N+1's tick-boundary drain. Step once
    // more so the last emit propagates.
    engine->step();

    std::printf("[test_asset_reload] deliveries=%d\n",
                deliveries.load(std::memory_order_relaxed));
    // Each F12 press triggers exactly one AssetReloaded emit via the
    // installed synthetic callback.
    CHECK_EQ(deliveries.load(), 3);

    EXIT_WITH_RESULT();
}
