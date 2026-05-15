// §3.6 batch 13b: sharded commit path correctness.
//
// Reruns the five churn scenarios from `commit_hash_test.cpp` twice
// each — once with `Config::singleThreadedCommit = true` (the
// reference path), once with `= false` (the sharded path) — and
// asserts:
//
//   (1) per-tick `EngineStats::commitHash` matches across the two
//       paths across every (scenario, tick) pair;
//   (2) the final `WorldSnapshot` serialized-bytes FNV-1a-64 matches
//       across the two paths;
//   (3) the same scenario run twice under the sharded path produces
//       byte-identical results (re-stability across runs);
//   (4) the four value-only command types (SetTransform / SetVelocity /
//       SetAcceleration / SetUserData) take the chunk-local fast lane
//       — exercised indirectly by the integration scenario, where the
//       sharded path's parallelism win comes from these.
//
// Determinism is the load-bearing contract: the sharded path is a
// pure performance opt-in that MUST produce the same world state
// bit-for-bit. Any divergence is a correctness regression.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace threadmaxx;

// ---------------------------------------------------------------------
// FNV-1a-64 over a WorldSnapshot's serialized bytes. Independent
// reference path — the engine never sees this helper.
std::uint64_t snapshotHash(const WorldSnapshot& s) {
    std::ostringstream os(std::ios::binary);
    serialize(os, s);
    const std::string blob = os.str();
    std::uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : blob) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return h;
}

// ---------------------------------------------------------------------
// Churn system — mirrors `commit_hash_test.cpp`'s scenarios so the
// sharded path is hash-compared against the same workloads the
// single-threaded reference was validated under.
class ChurnSystem : public ISystem {
public:
    ChurnSystem(int scenario, std::vector<EntityHandle> entities)
        : scenario_(scenario), entities_(std::move(entities)) {}

    const char* name() const noexcept override { return "churn"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::all(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }

    void update(SystemContext& ctx) override {
        const std::uint64_t t = ctx.tick();
        ctx.single([this, t](Range, CommandBuffer& cb) {
            applyMutations(cb, t);
        });
    }

private:
    void applyMutations(CommandBuffer& cb, std::uint64_t tick) {
        const std::size_t n = entities_.size();
        switch (scenario_) {
        case 0: {
            // Transform-only integration — the canonical sharded
            // fast-path workload (100 % value-only commands, all
            // chunk-local).
            for (std::size_t i = 0; i < n; ++i) {
                Transform t{};
                t.position.x = static_cast<float>((i + tick) & 0xFF);
                t.position.y = static_cast<float>((i * 3 + tick) & 0xFF);
                cb.setTransform(entities_[i], t);
            }
            break;
        }
        case 1: {
            for (std::size_t i = 0; i < n; ++i) {
                Transform t{};
                t.position.x = static_cast<float>((i + tick) & 0xFF);
                t.position.y = static_cast<float>(tick & 0xFFFF) * 0.5f;
                cb.setTransform(entities_[i], t);
                if ((i + tick) % 2 == 0) {
                    Health h;
                    h.current = static_cast<float>((i + tick) & 0x3F);
                    h.max     = 100.0f + static_cast<float>(tick & 0xFF);
                    cb.setHealth(entities_[i], h);
                }
            }
            break;
        }
        case 2: {
            for (std::size_t i = 0; i < n; ++i) {
                Transform t{};
                t.position.z = static_cast<float>((i ^ tick) & 0xFF);
                cb.setTransform(entities_[i], t);
                if ((i + tick) % 4 == 0) {
                    cb.addTag(entities_[i], Component::StaticTag);
                } else if ((i + tick) % 4 == 1) {
                    cb.removeTag(entities_[i], Component::StaticTag);
                }
            }
            break;
        }
        case 3: {
            for (std::size_t i = 0; i < n; ++i) {
                Parent p;
                p.parent                 = entities_[(i + tick) % n];
                p.localOffset            = {};
                p.localOffset.position.y = static_cast<float>(tick & 0xFF);
                cb.setParent(entities_[i], p);
            }
            break;
        }
        case 4: {
            for (std::size_t i = 0; i < n; ++i) {
                Velocity v;
                v.linear.x = static_cast<float>((i + tick) % 17);
                cb.setVelocity(entities_[i], v);
                if ((i + tick) % 3 == 0) {
                    BoundingVolume bv;
                    bv.min = {static_cast<float>(tick), 0.0f, 0.0f};
                    bv.max = {static_cast<float>(tick + 1),
                              static_cast<float>(i & 0xF), 1.0f};
                    cb.setBoundingVolume(entities_[i], bv);
                }
                if ((i + tick) % 5 == 0) {
                    RenderTag rt;
                    rt.meshId     = static_cast<std::int32_t>((i + tick) & 0xFF);
                    rt.materialId = static_cast<std::int32_t>(tick & 0x7);
                    rt.flags      = static_cast<std::uint32_t>(i & 0xFF);
                    cb.setRenderTag(entities_[i], rt);
                }
            }
            break;
        }
        }
    }

    int scenario_;
    std::vector<EntityHandle> entities_;
};

struct SeedGame : IGame {
    std::size_t count = 0;
    void onSetup(Engine&, World&, CommandBuffer& cb) override {
        for (std::size_t i = 0; i < count; ++i) {
            Transform t{};
            t.position.x = static_cast<float>(i);
            cb.spawn(t, Velocity{}, RenderTag{},
                     UserData{static_cast<std::uint64_t>(i)});
        }
    }
};

struct RunResult {
    std::vector<std::uint64_t> tickHashes;   // size == kTicks
    std::uint64_t              snapshotHash = 0;
};

constexpr int kTicks = 256;

RunResult runScenario(int scenario, std::size_t entityCount,
                      bool shardedCommit) {
    Config cfg;
    cfg.sleepToPace          = false;
    // Multi-worker so the sharded path actually fans out across
    // workers. The reference path is unaffected by worker count.
    cfg.workerCount          = 4;
    cfg.deterministic        = true;
    cfg.singleThreadedCommit = !shardedCommit;
    Engine engine(cfg);

    SeedGame game;
    game.count = entityCount;
    CHECK(engine.initialize(game));

    std::vector<EntityHandle> seeded;
    {
        const auto span = engine.world().entities();
        seeded.assign(span.begin(), span.end());
    }
    CHECK_EQ(seeded.size(), entityCount);

    engine.registerSystem(std::make_unique<ChurnSystem>(scenario, seeded));

    RunResult out;
    out.tickHashes.reserve(kTicks);
    for (int i = 0; i < kTicks; ++i) {
        engine.step();
        out.tickHashes.push_back(engine.stats().commitHash);
    }

    out.snapshotHash = snapshotHash(engine.world().snapshot());
    engine.shutdown();
    return out;
}

} // namespace

int main() {
    using namespace threadmaxx;

    struct ScenarioSpec {
        const char* name;
        int         scenario;
        std::size_t entityCount;
    };
    const ScenarioSpec specs[] = {
        {"transform-only",       0,  1024},
        {"transform+health",     1,  8192},
        {"transform+statictag",  2, 32768},
        {"parent-hierarchy",     3,  1024},
        {"mixed-multi-setter",   4,  8192},
    };

    // ---- (1) Sharded == Reference (per-tick + final snapshot) -------
    //
    // The strong invariant: the sharded path produces bit-for-bit
    // identical state vs. the single-threaded reference path across
    // every (scenario, tick) pair.
    for (const auto& spec : specs) {
        const auto refRun  = runScenario(spec.scenario, spec.entityCount, /*sharded=*/false);
        const auto shdRun  = runScenario(spec.scenario, spec.entityCount, /*sharded=*/true);

        CHECK_EQ(refRun.tickHashes.size(), static_cast<std::size_t>(kTicks));
        CHECK_EQ(shdRun.tickHashes.size(), static_cast<std::size_t>(kTicks));

        for (int t = 0; t < kTicks; ++t) {
            if (refRun.tickHashes[t] != shdRun.tickHashes[t]) {
                std::fprintf(stderr,
                    "scenario=%s tick=%d ref=0x%016llx shd=0x%016llx\n",
                    spec.name, t,
                    (unsigned long long)refRun.tickHashes[t],
                    (unsigned long long)shdRun.tickHashes[t]);
            }
            CHECK_EQ(refRun.tickHashes[t], shdRun.tickHashes[t]);
        }
        CHECK_EQ(refRun.snapshotHash, shdRun.snapshotHash);
    }

    // ---- (2) Sharded path is run-vs-run stable ----------------------
    //
    // Multi-worker scheduling is racy by design, but the chunk-local
    // bins partition writes by archetype so per-chunk apply order is
    // submission order. Two runs of the same scenario must produce
    // identical hashes.
    for (const auto& spec : specs) {
        const auto a = runScenario(spec.scenario, spec.entityCount, /*sharded=*/true);
        const auto b = runScenario(spec.scenario, spec.entityCount, /*sharded=*/true);
        for (int t = 0; t < kTicks; ++t) {
            CHECK_EQ(a.tickHashes[t], b.tickHashes[t]);
        }
        CHECK_EQ(a.snapshotHash, b.snapshotHash);
    }

    // ---- (3) Sharded path handles the empty-buffer case ------------
    //
    // A wave with no command emissions must not crash and must leave
    // `commitHash` unchanged from the basis.
    {
        Config cfg;
        cfg.sleepToPace          = false;
        cfg.workerCount          = 2;
        cfg.singleThreadedCommit = false;
        Engine engine(cfg);
        struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } g;
        CHECK(engine.initialize(g));
        engine.step();
        // No commands → hash stays at FNV-1a basis.
        CHECK_EQ(engine.stats().commitHash, 0xcbf29ce484222325ull);
        engine.shutdown();
    }

    // ---- (4) Sharded path tolerates stale handles in chunk-local lane
    //
    // Destroy an entity in tick N, then setTransform it in tick N+1 — the
    // sharded path's locate(stale) bounds check routes through the global
    // lane where applyCommandImpl no-ops on a missing slot.
    {
        Config cfg;
        cfg.sleepToPace          = false;
        cfg.workerCount          = 2;
        cfg.singleThreadedCommit = false;
        Engine engine(cfg);
        SeedGame g; g.count = 4;
        CHECK(engine.initialize(g));
        std::vector<EntityHandle> seeded;
        {
            const auto span = engine.world().entities();
            seeded.assign(span.begin(), span.end());
        }

        class DestroyThenSet : public ISystem {
        public:
            std::vector<EntityHandle> es;
            std::uint64_t step = 0;
            const char* name() const noexcept override { return "destroy-then-set"; }
            ComponentSet reads()  const noexcept override { return ComponentSet::all(); }
            ComponentSet writes() const noexcept override { return ComponentSet::all(); }
            void update(SystemContext& ctx) override {
                const auto t = ctx.tick();
                ctx.single([this, t](Range, CommandBuffer& cb) {
                    if (t == 0) {
                        cb.destroy(es[0]);
                    } else if (t == 1) {
                        // Stale handle write: should no-op cleanly.
                        Transform tr{};
                        tr.position.x = 99.0f;
                        cb.setTransform(es[0], tr);
                    }
                });
                (void)step;
            }
        };
        auto sys = std::make_unique<DestroyThenSet>();
        sys->es = seeded;
        engine.registerSystem(std::move(sys));

        engine.step();   // destroys es[0]
        engine.step();   // tries to set transform on stale handle — must not crash
        CHECK_EQ(engine.world().size(), std::size_t{3});

        engine.shutdown();
    }

    EXIT_WITH_RESULT();
}
