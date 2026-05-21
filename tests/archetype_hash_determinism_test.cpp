// §3.6 batch 30 — per-archetype hash rollup determinism.
//
// Pins the new contract on `EngineStats::commitHash` (default
// `Config::legacyCommitHash = false`):
//
//   (1) Same input → same per-tick hashes across two runs.
//   (2) Same final per-archetype state via a different command stream
//       still produces the same hash (the C2 contract amendment —
//       command order is no longer load-bearing).
//   (3) Different observable state → different hash.
//   (4) An "empty world" (only the initial all-mask chunk, never any
//       spawned entity) still hashes deterministically, and two empty
//       steps don't drift.
//   (5) `legacyCommitHash = true` reproduces the v1.x byte-mix path
//       across two runs (run-vs-run equivalence; specific values
//       differ from the new path by construction).

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <vector>

namespace {

using namespace threadmaxx;

// Seeds N entities in a single archetype (Transform+Velocity+Health).
class SeedGame : public IGame {
public:
    std::size_t count = 0;
    void onSetup(Engine&, World&, CommandBuffer& cb) override {
        for (std::size_t i = 0; i < count; ++i) {
            const float fi = static_cast<float>(i);
            auto b = bundle(
                Transform{ {fi, 0.0f, 0.0f}, {}, {1.0f, 1.0f, 1.0f} },
                Velocity{ {0.1f, 0.0f, 0.0f}, {} },
                Health{ 100.0f, 100.0f });
            cb.spawnBundle(b);
        }
    }
};

// Writes Transform deltas in the order [0..N) — i.e. submission order
// matches entity order.
class ForwardSystem : public ISystem {
public:
    std::vector<EntityHandle> entities;
    const char* name() const noexcept override { return "forward"; }
    ComponentSet reads() const noexcept override { return ComponentSet::all(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        ctx.single([this](Range, CommandBuffer& cb) {
            for (std::size_t i = 0; i < entities.size(); ++i) {
                Transform t;
                t.position.x = static_cast<float>(i) + 1.0f;
                cb.setTransform(entities[i], t);
            }
        });
    }
};

// Same final state as ForwardSystem, but writes the per-entity Transform
// in REVERSE submission order. Final per-chunk state is identical, so
// the rollup hash must match the forward case under the new contract.
class ReverseSystem : public ISystem {
public:
    std::vector<EntityHandle> entities;
    const char* name() const noexcept override { return "reverse"; }
    ComponentSet reads() const noexcept override { return ComponentSet::all(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        ctx.single([this](Range, CommandBuffer& cb) {
            for (std::size_t i = entities.size(); i-- > 0; ) {
                Transform t;
                t.position.x = static_cast<float>(i) + 1.0f;
                cb.setTransform(entities[i], t);
            }
        });
    }
};

// Run a single fixed scenario, return per-tick commitHashes.
struct RunOutput {
    std::vector<std::uint64_t> tickHashes;
    std::uint64_t              finalHash{};
};

RunOutput runForward(std::size_t entityCount, int ticks,
                     bool legacyHash, bool sharded) {
    Config cfg;
    cfg.sleepToPace          = false;
    cfg.workerCount          = 2;
    cfg.singleThreadedCommit = !sharded;
    cfg.legacyCommitHash     = legacyHash;
    Engine engine(cfg);

    SeedGame g;
    g.count = entityCount;
    CHECK(engine.initialize(g));

    std::vector<EntityHandle> seeded;
    {
        const auto span = engine.world().entities();
        seeded.assign(span.begin(), span.end());
    }

    auto sys = std::make_unique<ForwardSystem>();
    sys->entities = seeded;
    engine.registerSystem(std::move(sys));

    RunOutput out;
    out.tickHashes.reserve(ticks);
    for (int t = 0; t < ticks; ++t) {
        engine.step();
        out.tickHashes.push_back(engine.stats().commitHash);
    }
    out.finalHash = out.tickHashes.back();
    engine.shutdown();
    return out;
}

RunOutput runReverse(std::size_t entityCount, int ticks,
                     bool legacyHash, bool sharded) {
    Config cfg;
    cfg.sleepToPace          = false;
    cfg.workerCount          = 2;
    cfg.singleThreadedCommit = !sharded;
    cfg.legacyCommitHash     = legacyHash;
    Engine engine(cfg);

    SeedGame g;
    g.count = entityCount;
    CHECK(engine.initialize(g));

    std::vector<EntityHandle> seeded;
    {
        const auto span = engine.world().entities();
        seeded.assign(span.begin(), span.end());
    }

    auto sys = std::make_unique<ReverseSystem>();
    sys->entities = seeded;
    engine.registerSystem(std::move(sys));

    RunOutput out;
    out.tickHashes.reserve(ticks);
    for (int t = 0; t < ticks; ++t) {
        engine.step();
        out.tickHashes.push_back(engine.stats().commitHash);
    }
    out.finalHash = out.tickHashes.back();
    engine.shutdown();
    return out;
}

} // namespace

int main() {
    constexpr std::size_t kEntities = 64;
    constexpr int         kTicks    = 32;

    // ---- (1) Same input → same per-tick hashes (new path) -------------
    {
        const auto a = runForward(kEntities, kTicks, /*legacy=*/false, /*sharded=*/false);
        const auto b = runForward(kEntities, kTicks, /*legacy=*/false, /*sharded=*/false);
        for (int t = 0; t < kTicks; ++t) {
            CHECK_EQ(a.tickHashes[t], b.tickHashes[t]);
        }
    }

    // ---- (2) Different command order, same final state → same hash ----
    //
    // The contract amendment is exactly this: forward / reverse writes
    // produce the same final per-entity Transform values (because each
    // tick everyone gets t.position.x = idx+1), so the per-archetype
    // state hash must match. Under the v1.x legacy hash these would
    // differ (different command order → different byte-mix order).
    {
        const auto fwd = runForward(kEntities, kTicks, /*legacy=*/false, /*sharded=*/false);
        const auto rev = runReverse(kEntities, kTicks, /*legacy=*/false, /*sharded=*/false);
        for (int t = 0; t < kTicks; ++t) {
            CHECK_EQ(fwd.tickHashes[t], rev.tickHashes[t]);
        }

        // Cross-check: under legacyCommitHash=true the same two runs
        // produce DIFFERENT hashes because the byte-mix is order-
        // dependent. This pins the v1.x semantic for the deprecation
        // window.
        const auto fwdLeg = runForward(kEntities, kTicks, /*legacy=*/true, /*sharded=*/false);
        const auto revLeg = runReverse(kEntities, kTicks, /*legacy=*/true, /*sharded=*/false);
        bool anyDiff = false;
        for (int t = 0; t < kTicks; ++t) {
            if (fwdLeg.tickHashes[t] != revLeg.tickHashes[t]) { anyDiff = true; break; }
        }
        CHECK(anyDiff);
    }

    // ---- (3) Different observable state → different hash --------------
    {
        const auto a = runForward(kEntities,        kTicks, /*legacy=*/false, /*sharded=*/false);
        const auto b = runForward(kEntities + 1u,   kTicks, /*legacy=*/false, /*sharded=*/false);
        CHECK(a.finalHash != b.finalHash);
    }

    // ---- (4) Empty world is deterministic and stable across steps -----
    {
        Config cfg;
        cfg.sleepToPace = false;
        cfg.workerCount = 1;
        Engine engine(cfg);
        struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } g;
        CHECK(engine.initialize(g));
        engine.step();
        const std::uint64_t h1 = engine.stats().commitHash;
        engine.step();
        const std::uint64_t h2 = engine.stats().commitHash;
        engine.step();
        const std::uint64_t h3 = engine.stats().commitHash;
        CHECK_EQ(h1, h2);
        CHECK_EQ(h2, h3);
        engine.shutdown();
    }

    // ---- (5) Legacy hash path: run-vs-run determinism ------------------
    //
    // Independently of the new path, the v1.x byte-mix hash is also
    // deterministic across runs of the same command stream. The
    // specific values DIFFER from the new path by construction (the
    // hash is computed differently); we only check internal
    // determinism here, not cross-path agreement.
    {
        const auto a = runForward(kEntities, kTicks, /*legacy=*/true, /*sharded=*/false);
        const auto b = runForward(kEntities, kTicks, /*legacy=*/true, /*sharded=*/false);
        for (int t = 0; t < kTicks; ++t) {
            CHECK_EQ(a.tickHashes[t], b.tickHashes[t]);
        }

        // The two hash paths produce DIFFERENT final values for the
        // same command stream — they're hashing different things by
        // construction. Pin that they don't accidentally coincide.
        const auto neu = runForward(kEntities, kTicks, /*legacy=*/false, /*sharded=*/false);
        CHECK(a.finalHash != neu.finalHash);
    }

    // ---- (6) Sharded path agrees with single-threaded under new hash --
    //
    // Both apply the same commands, ending in the same per-archetype
    // state. The rollup must match per-tick — this is the cross-path
    // contract the sharded_commit_test pins for the legacy path, now
    // re-verified for the new path.
    {
        const auto serial  = runForward(kEntities, kTicks, /*legacy=*/false, /*sharded=*/false);
        const auto shardd  = runForward(kEntities, kTicks, /*legacy=*/false, /*sharded=*/true);
        for (int t = 0; t < kTicks; ++t) {
            CHECK_EQ(serial.tickHashes[t], shardd.tickHashes[t]);
        }
    }

    EXIT_WITH_RESULT();
}
