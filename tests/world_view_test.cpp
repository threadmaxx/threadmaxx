// §3.6 batch 13c — WorldView contract: a wave-scoped read-only snapshot
// of the world's chunk inventory. Stable across multiple parallelFor /
// single calls within a single update(); rebuilt before each wave.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>
#include <cstdint>
#include <vector>

namespace {

using namespace threadmaxx;

class InspectorSystem : public ISystem {
public:
    const char* name() const noexcept override { return "inspect"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }

    void update(SystemContext& ctx) override {
        const auto& view = ctx.worldView();
        viewChunkCount   = view.chunkCount();
        viewEntityCount  = view.entityCount();
        worldHasMatching = (view.world() == &ctx.world());

        // Capture chunk pointers via the cheap span; verify each pointer
        // is non-null and the total row count equals view.entityCount().
        auto chunks = view.chunks();
        std::size_t totalRows = 0;
        for (const auto* c : chunks) {
            CHECK(c != nullptr);
            totalRows += c->entities.size();
        }
        chunkPointersOk = (totalRows == view.entityCount());

        // Multiple parallelFor calls must see the same view contents
        // — the engine never rebuilds it mid-wave.
        std::atomic<std::size_t> firstPassChunkCount{0};
        std::atomic<std::size_t> secondPassChunkCount{0};
        ctx.parallelFor(1, 1, [&](Range, CommandBuffer&) {
            firstPassChunkCount.store(ctx.worldView().chunkCount(),
                                      std::memory_order_release);
        });
        ctx.parallelFor(1, 1, [&](Range, CommandBuffer&) {
            secondPassChunkCount.store(ctx.worldView().chunkCount(),
                                       std::memory_order_release);
        });
        passesAgree = (firstPassChunkCount.load() == secondPassChunkCount.load());
    }

    std::size_t viewChunkCount   = 0;
    std::size_t viewEntityCount  = 0;
    bool        worldHasMatching = false;
    bool        chunkPointersOk  = false;
    bool        passesAgree      = false;
};

struct SeedGame : IGame {
    InspectorSystem* sys = nullptr;
    std::size_t      seedCount = 0;
    bool             mixArchetypes = true;

    void onSetup(Engine& eng, World&, CommandBuffer& cb) override {
        for (std::size_t i = 0; i < seedCount; ++i) {
            Transform t{};
            t.position.x = static_cast<float>(i);
            // Mix two archetypes: half carry Health, half don't. With
            // chunked storage this yields exactly 2 distinct chunks.
            if (mixArchetypes && (i & 1) == 1) {
                Bundle b{};
                b.transform   = t;
                b.health      = Health{100.0f, 100.0f};
                b.initialMask = Component::Transform | Component::Health;
                cb.spawnBundle(b);
            } else {
                cb.spawn(t);
            }
        }
        auto inspector = std::make_unique<InspectorSystem>();
        sys = inspector.get();
        eng.registerSystem(std::move(inspector));
    }
};

} // namespace

int main() {
    // The engine eagerly creates an empty mask=0 chunk during world
    // construction, so `archetypeChunkCount()` is always at least 1
    // once a world exists. The view mirrors that count; we assert
    // exact equality to the World accessor rather than a literal.

    // ---- (1) Empty world -----------------------------------------------
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 2;
        Engine engine(cfg);
        SeedGame g; g.seedCount = 0; g.mixArchetypes = false;
        CHECK(engine.initialize(g));
        engine.step();
        CHECK_EQ(g.sys->viewEntityCount, std::size_t{0});
        CHECK_EQ(g.sys->viewChunkCount,
                 engine.world().archetypeChunkCount());
        CHECK(g.sys->worldHasMatching);
        CHECK(g.sys->chunkPointersOk);
        CHECK(g.sys->passesAgree);
        engine.shutdown();
    }

    // ---- (2) Single archetype, many entities --------------------------
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 2;
        Engine engine(cfg);
        SeedGame g; g.seedCount = 4096; g.mixArchetypes = false;
        CHECK(engine.initialize(g));
        engine.step();
        CHECK_EQ(g.sys->viewEntityCount, std::size_t{4096});
        CHECK_EQ(g.sys->viewChunkCount,
                 engine.world().archetypeChunkCount());
        CHECK(g.sys->viewChunkCount >= std::size_t{1});
        CHECK(g.sys->chunkPointersOk);
        CHECK(g.sys->passesAgree);
        engine.shutdown();
    }

    // ---- (3) Two archetypes ------------------------------------------
    {
        Config cfg; cfg.sleepToPace = false; cfg.workerCount = 2;
        Engine engine(cfg);
        SeedGame g; g.seedCount = 2048; g.mixArchetypes = true;
        CHECK(engine.initialize(g));
        engine.step();
        CHECK_EQ(g.sys->viewEntityCount, std::size_t{2048});
        CHECK_EQ(g.sys->viewChunkCount,
                 engine.world().archetypeChunkCount());
        CHECK(g.sys->viewChunkCount >= std::size_t{2});
        CHECK(g.sys->chunkPointersOk);
        CHECK(g.sys->passesAgree);
        engine.shutdown();
    }

    EXIT_WITH_RESULT();
}
