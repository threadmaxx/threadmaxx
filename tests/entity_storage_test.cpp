// Exercises spawn/destroy/swap-and-pop and generation invalidation through
// the public Engine surface (which is what real users see).

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <algorithm>
#include <vector>

namespace {

// A system that does nothing on update — we only need the engine to run a
// commit phase. We drive commits by issuing commands from onSetup via the
// seed buffer.
class NoopSystem : public threadmaxx::ISystem {
    const char* name() const noexcept override { return "noop"; }
    void update(threadmaxx::SystemContext&) override {}
};

class CapturingGame : public threadmaxx::IGame {
public:
    void onSetup(threadmaxx::Engine& engine, threadmaxx::World&,
                 threadmaxx::CommandBuffer& seed) override {
        engine.registerSystem(std::make_unique<NoopSystem>());
        // Spawn five entities. We tag each with a distinguishing UserData
        // value so we can identify them after swap-and-pop.
        for (int i = 0; i < 5; ++i) {
            threadmaxx::Transform t;
            t.position = {static_cast<float>(i), 0.0f, 0.0f};
            threadmaxx::RenderTag tag;
            tag.meshId = i;  // distinct
            seed.spawn(t, {}, tag, threadmaxx::UserData{static_cast<std::uint64_t>(i)});
        }
    }
};

} // namespace

int main() {
    threadmaxx::Config cfg;
    cfg.sleepToPace = false;
    cfg.initialEntityCapacity = 16;
    threadmaxx::Engine engine(cfg);
    CapturingGame game;
    CHECK(engine.initialize(game));

    // After setup-commit, world has 5 live entities.
    auto& world = engine.world();
    CHECK_EQ(world.size(), std::size_t{5});

    // Snapshot handles. Order of handles == order of seed.spawn() calls
    // (commit applies them in submission order).
    std::vector<threadmaxx::EntityHandle> handles(
        world.entities().begin(), world.entities().end());
    CHECK_EQ(handles.size(), std::size_t{5});

    // Each entity's UserData reflects the index it was spawned at.
    for (std::size_t i = 0; i < handles.size(); ++i) {
        const auto* u = world.tryGetUserData(handles[i]);
        CHECK(u != nullptr);
        if (u) CHECK_EQ(u->value, static_cast<std::uint64_t>(i));
    }

    // Destroy the middle entity via a system+command flow. Easiest: run a
    // step that does no work, but feed a kill command in through a tiny
    // ad-hoc system. Instead we'll go via a quick "kill" system added below.

    // Simpler: use an inline approach — register a one-shot system that
    // destroys handles[2] on its first update, then run one step.
    struct KillOne : public threadmaxx::ISystem {
        threadmaxx::EntityHandle target;
        bool done = false;
        const char* name() const noexcept override { return "kill"; }
        void update(threadmaxx::SystemContext& ctx) override {
            if (done) return;
            const auto t = target;
            ctx.single([t](threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
                cb.destroy(t);
            });
            done = true;
        }
    };
    auto kill = std::make_unique<KillOne>();
    kill->target = handles[2];
    engine.registerSystem(std::move(kill));
    engine.step();

    // Post-destroy: size == 4, target handle no longer alive, swap-and-pop
    // brought what was the last entity into slot 2.
    CHECK_EQ(world.size(), std::size_t{4});
    CHECK(!world.alive(handles[2]));
    for (int i = 0; i < 5; ++i) {
        if (i == 2) continue;
        CHECK(world.alive(handles[i]));
    }

    // After swap-and-pop the dense array should have entries with userData
    // values {0, 1, 4, 3} (we swapped index 4 into slot 2). Verify by set
    // membership rather than order to keep the test robust to future
    // ordering tweaks.
    std::vector<std::uint64_t> seen;
    for (const auto& u : world.userData()) seen.push_back(u.value);
    std::sort(seen.begin(), seen.end());
    CHECK_EQ(seen.size(), std::size_t{4});
    CHECK_EQ(seen[0], std::uint64_t{0});
    CHECK_EQ(seen[1], std::uint64_t{1});
    CHECK_EQ(seen[2], std::uint64_t{3});
    CHECK_EQ(seen[3], std::uint64_t{4});

    // Render tags should still be coherent with their entities (per-entity
    // lookup via tryGetRenderTag should match the original meshId).
    for (int i = 0; i < 5; ++i) {
        if (i == 2) continue;
        const auto* rt = world.tryGetRenderTag(handles[i]);
        CHECK(rt != nullptr);
        if (rt) CHECK_EQ(rt->meshId, i);
    }

    // Generation invalidation: spawn a new entity. It is allowed to reuse
    // the freed slot, but the original handle must remain invalid.
    struct SpawnOne : public threadmaxx::ISystem {
        bool done = false;
        const char* name() const noexcept override { return "spawn"; }
        void update(threadmaxx::SystemContext& ctx) override {
            if (done) return;
            ctx.single([](threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
                threadmaxx::RenderTag rt; rt.meshId = 99;
                cb.spawn({}, {}, rt, threadmaxx::UserData{99});
            });
            done = true;
        }
    };
    engine.registerSystem(std::make_unique<SpawnOne>());
    engine.step();

    CHECK_EQ(world.size(), std::size_t{5});
    CHECK(!world.alive(handles[2]));  // stale handle still invalid

    engine.shutdown();
    EXIT_WITH_RESULT();
}
