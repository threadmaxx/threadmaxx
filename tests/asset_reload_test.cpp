// §3.2 batch 7: hot-reload protocol. engine.markResourceStale(id)
// dispatches to every loader's markStale; loaders that recognize the
// type queue a reload and on the next update() install a fresh id and
// publish AssetReloaded on the engine's typed channel.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>
#include <optional>

namespace {

struct Mesh {
    int payload = 0;
};

class MeshLoader : public threadmaxx::IResourceLoader {
public:
    int meshTagToInstall = 100;
    threadmaxx::ResourceId<Mesh> liveId{};

    std::optional<std::pair<std::uint32_t, std::uint32_t>> pendingReload;
    int markStaleCallsForOtherType = 0;
    int markStaleCallsForUnknownGen = 0;

    void update(threadmaxx::Engine& engine) override {
        // Boot: install the initial mesh on tick 1.
        if (!liveId.valid()) {
            liveId = engine.resources().add(Mesh{meshTagToInstall++});
            return;
        }
        // Hot reload: install a new mesh, emit AssetReloaded, retire old.
        if (pendingReload) {
            const auto newId = engine.resources().add(Mesh{meshTagToInstall++});
            engine.events<threadmaxx::AssetReloaded>().emit(
                threadmaxx::AssetReloaded{
                    pendingReload->first,
                    pendingReload->second,
                    newId.index,
                    newId.generation,
                    std::type_index(typeid(Mesh)),
                });
            engine.resources().remove(liveId);
            liveId = newId;
            pendingReload.reset();
        }
    }

    void markStale(std::uint32_t index,
                   std::uint32_t generation,
                   std::type_index type) override {
        if (type != std::type_index(typeid(Mesh))) {
            ++markStaleCallsForOtherType;
            return;
        }
        if (!liveId.valid() ||
            index != liveId.index ||
            generation != liveId.generation) {
            ++markStaleCallsForUnknownGen;
            return;
        }
        pendingReload = {index, generation};
    }
};

} // namespace

int main() {
    using namespace threadmaxx;

    Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
    Engine engine(cfg);

    struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } game;
    CHECK(engine.initialize(game));

    auto* loader = static_cast<MeshLoader*>(
        engine.addResourceLoader(std::make_unique<MeshLoader>()));

    // Capture AssetReloaded events as they fly past.
    std::atomic<int> reloadEvents{0};
    AssetReloaded lastEvent{};
    auto& reloadCh = engine.events<AssetReloaded>();
    const auto subId = reloadCh.subscribe([&](const AssetReloaded& e) {
        ++reloadEvents;
        lastEvent = e;
    });

    // Tick 1: loader installs initial mesh.
    engine.step();
    const auto initialId = loader->liveId;
    CHECK(initialId.valid());
    CHECK(engine.resources().get(initialId) != nullptr);
    CHECK_EQ(engine.resources().get(initialId)->payload, 100);

    // markStale dispatches to loaders synchronously.
    engine.markResourceStale(initialId);
    CHECK(loader->pendingReload.has_value());

    // Tick 2: loader picks up the pending reload, installs new mesh,
    // emits AssetReloaded, drops the old id.
    engine.step();
    CHECK(!loader->pendingReload.has_value());
    const auto newId = loader->liveId;
    CHECK(newId.valid());
    CHECK(newId != initialId);

    // Drain happens at tick boundary; subscribers fire BEFORE the swap,
    // so reloadEvents has incremented by the time step() returns.
    CHECK_EQ(reloadEvents.load(), 1);
    CHECK(lastEvent.matches(initialId));
    CHECK_EQ(lastEvent.newIndex, newId.index);
    CHECK_EQ(lastEvent.newGeneration, newId.generation);

    // Old id is now stale.
    CHECK(engine.resources().get(initialId) == nullptr);
    CHECK(engine.resources().get(newId) != nullptr);
    CHECK_EQ(engine.resources().get(newId)->payload, 101);

    // markStale on a different type is dispatched but ignored by the
    // mesh loader (it bumps a counter).
    struct Other { int n; };
    ResourceId<Other> nonMatching{42, 1};
    engine.markResourceStale(nonMatching);
    CHECK_EQ(loader->markStaleCallsForOtherType, 1);

    // markStale on the now-stale id is dispatched but ignored
    // (loader's liveId is the newId; the stale generation no longer
    // matches its live one).
    engine.markResourceStale(initialId);
    CHECK_EQ(loader->markStaleCallsForUnknownGen, 1);

    reloadCh.unsubscribe(subId);
    engine.shutdown();
    EXIT_WITH_RESULT();
}
