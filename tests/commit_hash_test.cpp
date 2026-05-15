// §3.6 batch 13a: EngineStats::commitHash + Config::logCommitHashEvery.
//
// Covers:
//   - Five seeded churn scenarios (varying entity counts, varying mutation
//     mixes) each run twice for 256 ticks. The per-tick `commitHash` AND
//     the final `WorldSnapshot` FNV-1a hash must match across both runs.
//     This is the reference baseline that batch 13b's sharded commit path
//     will hash-compare against.
//   - A *different* command stream (one extra spawn) produces a
//     different hash — proof the field is actually accumulating
//     per-command state, not a constant.
//   - `Config::logCommitHashEvery = N` emits one Info-level record at
//     ticks that are multiples of N, each carrying the engine's
//     stats_.commitHash for that tick.
//
// Determinism note: today the commit path is single-threaded so
// run-vs-run agreement is expected by construction. The value of
// running this test now is that the moment batch 13b's sharded path
// lands, the same invariants run with the sharded path on and the test
// starts catching any reordering bug as a loud first-tick mismatch.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace threadmaxx;

// ---------------------------------------------------------------------
// FNV-1a-64 over a WorldSnapshot's serialized bytes. Independent
// reference path — the engine never sees this helper, so a match
// between this and commitHash means both are sound.
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
// Capturing logger so we can verify Config::logCommitHashEvery.
struct LogRecord {
    LogLevel    level;
    std::string message;
};

class CapturingLogger : public ILogger {
public:
    void log(LogLevel level, std::string_view msg) override {
        std::lock_guard<std::mutex> lk(mtx_);
        records_.push_back({level, std::string(msg)});
    }
    std::vector<LogRecord> records() {
        std::lock_guard<std::mutex> lk(mtx_);
        return records_;
    }
private:
    std::mutex             mtx_;
    std::vector<LogRecord> records_;
};

// ---------------------------------------------------------------------
// Churn system parameterized by a scenario id. Each scenario applies a
// deterministic per-tick mutation pattern keyed off the entity index +
// tick number; seeding is purely positional so two runs of the same
// scenario produce byte-identical commands.
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
            // Transform-only integration: every entity moves by a
            // deterministic per-(entity,tick) offset.
            for (std::size_t i = 0; i < n; ++i) {
                Transform t{};
                t.position.x = static_cast<float>((i + tick) & 0xFF);
                t.position.y = static_cast<float>((i * 3 + tick) & 0xFF);
                cb.setTransform(entities_[i], t);
            }
            break;
        }
        case 1: {
            // Transform + half the set flips the Health presence bit.
            // Use tick scaled so byte-mask wrap doesn't alias tick T
            // with tick T+128.
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
            // Transform + StaticTag flips. Tag-only category exercises
            // a different mask-flip path than dense-component setters.
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
            // Parent attachments: every entity reparents to (i+tick) % n.
            // Exercises the Parent setter's mask-flip + value write path.
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
            // Mixed: velocity + bounding-volume + render-tag flips.
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

// ---------------------------------------------------------------------
// IGame that seeds `count` entities (plus optionally one extra).
struct SeedGame : IGame {
    std::size_t count       = 0;
    bool        extraSpawn  = false;
    void onSetup(Engine&, World&, CommandBuffer& cb) override {
        for (std::size_t i = 0; i < count; ++i) {
            Transform t{};
            t.position.x = static_cast<float>(i);
            cb.spawn(t, Velocity{}, RenderTag{},
                     UserData{static_cast<std::uint64_t>(i)});
        }
        if (extraSpawn) {
            Transform t{};
            t.position.x = 999.0f;
            cb.spawn(t, Velocity{}, RenderTag{}, UserData{0xDEADBEEFull});
        }
    }
};

// ---------------------------------------------------------------------
// Build + run a scenario once. Returns (per-tick hashes, final snapshot
// hash). `extraSpawn=true` adds one extra spawn during setup — used to
// prove a different command stream yields a different hash.
struct RunResult {
    std::vector<std::uint64_t> tickHashes;   // size == kTicks
    std::uint64_t              snapshotHash = 0;
};

constexpr int kTicks = 256;

RunResult runScenario(int scenario, std::size_t entityCount,
                      bool extraSpawn = false) {
    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 1;          // determinism is unaffected by worker count
                                  // but pin for test stability
    cfg.deterministic = true;
    Engine engine(cfg);

    SeedGame game;
    game.count      = entityCount;
    game.extraSpawn = extraSpawn;
    CHECK(engine.initialize(game));

    // Collect the handles the seed buffer materialized — entities()
    // is stable until something mutates the world (we haven't yet).
    std::vector<EntityHandle> seeded;
    {
        const auto span = engine.world().entities();
        seeded.assign(span.begin(), span.end());
    }
    CHECK_EQ(seeded.size(), entityCount + (extraSpawn ? std::size_t{1} : std::size_t{0}));

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

    // ---- (1) Five seeded churn scenarios, each run twice ------------
    //
    // The five scenarios cover: transform-only, dense-component-flip,
    // tag-flip, parent-hierarchy, mixed multi-setter. Each entity-count
    // (1k / 8k / 32k) stresses a different chunk size; collectively they
    // catch any sharding-order bug batch 13b might introduce.
    //
    // 32k × 256 ticks is the upper end of what the test should pay; the
    // smaller scenarios run in milliseconds.

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

    for (const auto& spec : specs) {
        const auto a = runScenario(spec.scenario, spec.entityCount);
        const auto b = runScenario(spec.scenario, spec.entityCount);

        CHECK_EQ(a.tickHashes.size(), static_cast<std::size_t>(kTicks));
        CHECK_EQ(b.tickHashes.size(), static_cast<std::size_t>(kTicks));

        // Per-tick agreement across runs is the strong invariant — any
        // divergence after batch 13b lands is a sharding ordering bug.
        for (int t = 0; t < kTicks; ++t) {
            if (a.tickHashes[t] != b.tickHashes[t]) {
                std::fprintf(stderr,
                    "scenario=%s tick=%d hash mismatch a=0x%016llx b=0x%016llx\n",
                    spec.name, t,
                    (unsigned long long)a.tickHashes[t],
                    (unsigned long long)b.tickHashes[t]);
            }
            CHECK_EQ(a.tickHashes[t], b.tickHashes[t]);
        }

        // The final-state snapshot hashes must also match — the
        // commitHash and the snapshot hash are independent code paths
        // observing the same deterministic world.
        CHECK_EQ(a.snapshotHash, b.snapshotHash);

        // Hash should evolve over time, not be constant. Mid- and
        // end-of-run values should differ from tick 0 in every
        // scenario that mutates anything.
        CHECK(a.tickHashes[0] != a.tickHashes[kTicks / 2]);
        CHECK(a.tickHashes[0] != a.tickHashes[kTicks - 1]);
    }

    // ---- (2) Different command stream → different hash --------------
    //
    // Same scenario, one extra spawn — the resulting commitHash at every
    // tick must differ. Proves the field actually depends on the
    // applied commands, not just on the tick number.
    {
        const auto base  = runScenario(0, 256, /*extraSpawn=*/false);
        const auto extra = runScenario(0, 256, /*extraSpawn=*/true);
        // After tick 0, every per-tick hash should diverge because the
        // extra entity participates in the same churn pattern.
        CHECK(base.tickHashes[10]            != extra.tickHashes[10]);
        CHECK(base.tickHashes[kTicks - 1]    != extra.tickHashes[kTicks - 1]);
        CHECK(base.snapshotHash              != extra.snapshotHash);
    }

    // ---- (3) Pause leaves the hash at the FNV-1a offset basis --------
    //
    // A paused tick commits nothing, so the field is reset to the
    // basis. Matches the documented contract on EngineStats::commitHash.
    {
        Config cfg;
        cfg.sleepToPace = false;
        cfg.workerCount = 1;
        Engine engine(cfg);
        struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } g;
        CHECK(engine.initialize(g));
        engine.setPaused(true);
        engine.step();
        CHECK_EQ(engine.stats().commitHash, 0xcbf29ce484222325ull);
        engine.shutdown();
    }

    // ---- (4) Config::logCommitHashEvery emits at the expected cadence
    //
    // N=3, 12 ticks → expect 4 records at ticks 3, 6, 9, 12. Each record
    // must carry the engine's stats_.commitHash for that tick.
    {
        Config cfg;
        cfg.sleepToPace = false;
        cfg.workerCount = 1;
        cfg.logCommitHashEvery = 3;
        Engine engine(cfg);
        CapturingLogger sink;
        engine.setLogger(&sink);

        SeedGame game;
        game.count = 16;
        CHECK(engine.initialize(game));
        std::vector<EntityHandle> seeded;
        {
            const auto span = engine.world().entities();
            seeded.assign(span.begin(), span.end());
        }
        engine.registerSystem(std::make_unique<ChurnSystem>(0, seeded));

        std::vector<std::uint64_t> hashes;
        for (int i = 0; i < 12; ++i) {
            engine.step();
            hashes.push_back(engine.stats().commitHash);
        }

        const auto records = sink.records();
        // Filter records to just our commitHash lines (the engine also
        // emits initialize/registerSystem logs at Info elsewhere).
        std::vector<std::string> hashRecords;
        for (const auto& r : records) {
            if (r.level == LogLevel::Info &&
                r.message.find("commitHash") != std::string::npos) {
                hashRecords.push_back(r.message);
            }
        }

        // Expect 4 records (at ticks 3, 6, 9, 12).
        CHECK_EQ(hashRecords.size(), std::size_t{4});

        // Each record's hash field should match stats_.commitHash from
        // the corresponding tick.
        auto extractHash = [](const std::string& m) -> std::uint64_t {
            const auto p = m.find("hash=0x");
            if (p == std::string::npos) return 0;
            std::uint64_t v = 0;
            std::istringstream is(m.substr(p + 7));
            is >> std::hex >> v;
            return v;
        };
        if (hashRecords.size() == 4) {
            CHECK_EQ(extractHash(hashRecords[0]), hashes[2]);   // tick 3 -> idx 2
            CHECK_EQ(extractHash(hashRecords[1]), hashes[5]);
            CHECK_EQ(extractHash(hashRecords[2]), hashes[8]);
            CHECK_EQ(extractHash(hashRecords[3]), hashes[11]);
        }

        engine.shutdown();
    }

    // ---- (5) logCommitHashEvery = 0 (default) emits nothing ---------
    {
        Config cfg;
        cfg.sleepToPace = false;
        cfg.workerCount = 1;
        // logCommitHashEvery is 0 by default; assert that explicitly.
        CHECK_EQ(cfg.logCommitHashEvery, std::uint32_t{0});
        Engine engine(cfg);
        CapturingLogger sink;
        engine.setLogger(&sink);
        struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } g;
        CHECK(engine.initialize(g));
        for (int i = 0; i < 5; ++i) engine.step();
        const auto records = sink.records();
        for (const auto& r : records) {
            CHECK(r.message.find("commitHash") == std::string::npos);
        }
        engine.shutdown();
    }

    EXIT_WITH_RESULT();
}
