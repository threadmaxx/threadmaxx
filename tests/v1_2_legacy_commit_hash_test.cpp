// §3.6 batch 30 — legacy commit-hash path pinned.
//
// Pins the v1.x per-command byte-mix hash invariants under
// `Config::legacyCommitHash = true`. These checks USED to live in
// `commit_hash_test.cpp` and are duplicated here so the legacy
// behavior is independently verifiable for the duration of the v1.3
// deprecation window. When `legacyCommitHash` is removed, this test
// is the canonical artifact that goes away with it.
//
// Covers:
//   (1) Same input → same per-tick hashes across two runs.
//   (2) Different command stream → different hash (the v1.x "command
//       ordering is hash-load-bearing" invariant).
//   (3) An empty-step / paused-step is the FNV-1a-64 offset basis —
//       the v1.x "no commands committed → sentinel" contract.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <vector>

namespace {

using namespace threadmaxx;

class SeedGame : public IGame {
public:
    std::size_t count = 0;
    void onSetup(Engine&, World&, CommandBuffer& cb) override {
        for (std::size_t i = 0; i < count; ++i) {
            const float fi = static_cast<float>(i);
            auto b = bundle(
                Transform{ {fi, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f} },
                Velocity{ {0.1f, 0.0f, 0.0f}, {} });
            cb.spawnBundle(b);
        }
    }
};

class ChurnSystem : public ISystem {
public:
    std::vector<EntityHandle> entities;
    int extraTick = -1;  // tick at which to spawn an extra entity (-1 = never)
    const char* name() const noexcept override { return "churn"; }
    ComponentSet reads() const noexcept override { return ComponentSet::all(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        const std::uint64_t t = ctx.tick();
        ctx.single([this, t](Range, CommandBuffer& cb) {
            for (std::size_t i = 0; i < entities.size(); ++i) {
                Transform tr;
                tr.position.x = static_cast<float>(i) + static_cast<float>(t);
                cb.setTransform(entities[i], tr);
            }
            if (extraTick >= 0 && static_cast<std::uint64_t>(extraTick) == t) {
                cb.spawn(Transform{{999.0f, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f}});
            }
        });
    }
};

struct RunOutput {
    std::vector<std::uint64_t> tickHashes;
};

RunOutput runChurn(std::size_t entityCount, int ticks, int extraTick) {
    Config cfg;
    cfg.sleepToPace      = false;
    cfg.workerCount      = 2;
    cfg.legacyCommitHash = true;  // PIN: legacy v1.x byte-mix hash
    Engine engine(cfg);

    SeedGame g;
    g.count = entityCount;
    CHECK(engine.initialize(g));

    std::vector<EntityHandle> seeded;
    {
        const auto span = engine.world().entities();
        seeded.assign(span.begin(), span.end());
    }

    auto sys = std::make_unique<ChurnSystem>();
    sys->entities = seeded;
    sys->extraTick = extraTick;
    engine.registerSystem(std::move(sys));

    RunOutput out;
    out.tickHashes.reserve(ticks);
    for (int t = 0; t < ticks; ++t) {
        engine.step();
        out.tickHashes.push_back(engine.stats().commitHash);
    }
    engine.shutdown();
    return out;
}

} // namespace

int main() {
    using namespace threadmaxx;

    constexpr std::size_t kEntities = 32;
    constexpr int         kTicks    = 24;

    // ---- (1) Same input → same per-tick hashes -------------------------
    {
        const auto a = runChurn(kEntities, kTicks, /*extraTick=*/-1);
        const auto b = runChurn(kEntities, kTicks, /*extraTick=*/-1);
        for (int t = 0; t < kTicks; ++t) {
            CHECK_EQ(a.tickHashes[t], b.tickHashes[t]);
        }
    }

    // ---- (2) Extra spawn in tick N → divergent hash at that tick -----
    //
    // The v1.x byte-mix hash is RESET to the FNV basis at the start of
    // every step and accumulates only that step's commands. So an
    // extra spawn at tick 5 diverges the tick-5 hash; subsequent ticks
    // can re-converge if their command streams happen to match. The
    // ChurnSystem only iterates the original `entities` vector (it
    // never touches the extra), so ticks 6..N share the same
    // command stream between base and extra and produce identical
    // legacy hashes. The divergence at tick 5 is the v1.x contract:
    // the per-tick hash is sensitive to the command stream of THAT
    // tick, not the resulting state.
    {
        const auto base  = runChurn(kEntities, kTicks, /*extraTick=*/-1);
        const auto extra = runChurn(kEntities, kTicks, /*extraTick=*/5);
        // Ticks 0..4: identical (extra hasn't spawned yet).
        for (int t = 0; t < 5; ++t) {
            CHECK_EQ(base.tickHashes[t], extra.tickHashes[t]);
        }
        // Tick 5: diverged (extra has the extra spawn).
        CHECK(base.tickHashes[5] != extra.tickHashes[5]);
    }

    // ---- (3) Empty step → FNV basis (v1.x sentinel) --------------------
    {
        Config cfg;
        cfg.sleepToPace      = false;
        cfg.workerCount      = 1;
        cfg.legacyCommitHash = true;
        Engine engine(cfg);
        struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } g;
        CHECK(engine.initialize(g));
        engine.step();
        CHECK_EQ(engine.stats().commitHash, 0xcbf29ce484222325ull);
        engine.shutdown();
    }

    // ---- (4) Paused step → FNV basis (the documented contract) --------
    //
    // Pause short-circuits in step() and never reaches commitBuffer or
    // finalizeCommitHash, so the legacy and new paths behave the same
    // here: the basis sentinel.
    {
        Config cfg;
        cfg.sleepToPace      = false;
        cfg.workerCount      = 1;
        cfg.legacyCommitHash = true;
        Engine engine(cfg);
        struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } g;
        CHECK(engine.initialize(g));
        engine.setPaused(true);
        engine.step();
        CHECK_EQ(engine.stats().commitHash, 0xcbf29ce484222325ull);
        engine.shutdown();
    }

    EXIT_WITH_RESULT();
}
