/// @file test_editor_hotreload_event_observed.cpp
/// @brief E4 — AssetReloaded fires → pendingReloads() no longer
/// contains the path.

#include "Check.hpp"
#include "EditorTestFixture.hpp"

#include <threadmaxx_editor/hotreload.hpp>

#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/Resource.hpp>

namespace {

struct Texture { int width; int height; };

// Loader that, on receiving markStale, installs a new id and emits
// AssetReloaded on its next update tick.
struct PingPongLoader final : threadmaxx::IResourceLoader {
    bool stalePending{false};
    std::uint32_t oldIndex{0};
    std::uint32_t oldGeneration{0};
    std::type_index oldType{typeid(void)};

    void markStale(std::uint32_t index,
                   std::uint32_t generation,
                   std::type_index type) override {
        oldIndex = index;
        oldGeneration = generation;
        oldType = type;
        stalePending = true;
    }

    void update(threadmaxx::Engine& engine) override {
        if (!stalePending) return;
        stalePending = false;
        // Install a replacement value at a new slot.
        auto newId = engine.resources().add<Texture>(Texture{512, 512});
        threadmaxx::AssetReloaded evt;
        evt.oldIndex = oldIndex;
        evt.oldGeneration = oldGeneration;
        evt.newIndex = newId.index;
        evt.newGeneration = newId.generation;
        evt.type = oldType;
        engine.events<threadmaxx::AssetReloaded>().emit(evt);
    }
};

} // namespace

int main() {
    threadmaxx::editor::test::ScopedEngine env;

    env.engine().addResourceLoader(std::make_unique<PingPongLoader>());

    auto handle = env.engine().resources()
                      .addRefCounted<Texture>(Texture{256, 256});

    threadmaxx::editor::HotReloadController ctl{env.engine()};
    ctl.trackResource(handle.id(), "diffuse.png");

    auto r = ctl.requestReload({"diffuse.png", false});
    CHECK(r.ok);
    CHECK_EQ(ctl.pendingReloads().size(), 1u);

    // Step once: loader's update fires AssetReloaded → controller
    // subscription drains it on the engine's tick-boundary drain.
    env.engine().step();

    const auto remaining = ctl.pendingReloads();
    CHECK_EQ(remaining.size(), 0u);

    EXIT_WITH_RESULT();
}
