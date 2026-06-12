#include "Check.hpp"

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/Game.hpp>
#include <threadmaxx/Resource.hpp>
#include <threadmaxx/World.hpp>

#include "threadmaxx_assets/async_loader.hpp"
#include "threadmaxx_assets/engine_bridge.hpp"
#include "threadmaxx_assets/registry.hpp"

#define STR2(x) #x
#define STR(x) STR2(x)

using namespace threadmaxx::assets;

int main() {
    const std::string cubePath =
        std::string(STR(THREADMAXX_ASSETS_FIXTURES_DIR)) + "/cube.obj";

    AssetRegistry reg;
    AsyncLoader   async(reg, 2);

    auto bridge = std::make_unique<EngineAssetLoader>(reg, async);
    auto* bridgePtr = bridge.get();

    struct NoopGame : threadmaxx::IGame {
        void onSetup(threadmaxx::Engine&, threadmaxx::World&,
                     threadmaxx::CommandBuffer&) override {}
    } game;

    threadmaxx::Engine engine;
    engine.addResourceLoader(std::move(bridge));
    CHECK(engine.initialize(game));

    auto h = async.enqueueMesh(cubePath);
    CHECK(!h.valid());

    // Stepping the engine pumps the loader via IResourceLoader::update.
    AssetHandle<MeshData> ready;
    for (int i = 0; i < 200; ++i) {
        engine.step();
        ready = reg.findMesh(cubePath);
        if (ready.valid()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(ready.valid());

    // Stats surface through the bridge's IResourceLoader::stats override.
    const auto s = bridgePtr->stats();
    CHECK_EQ(s.failed, 0ull);
    CHECK(bridgePtr->updateCalls() > 0);

    engine.shutdown();

    EXIT_WITH_RESULT();
}
