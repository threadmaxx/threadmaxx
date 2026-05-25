// SHARDED_OPTIMISATION.md S16 — workload-aware auto fallthrough.
//
// Three things this test pins:
//
//   (1) HIGH-GLOBAL workload + workloadAwareCommit=true triggers
//       fallthrough: `workloadAwareFallthrough` and `fallbackCalls`
//       both bump on the gated call; `shardedCalls` stays at zero.
//
//   (2) LOW-GLOBAL workload + workloadAwareCommit=true runs sharded:
//       `workloadAwareFallthrough` stays at zero; the sharded path
//       handles every commit in full.
//
//   (3) Determinism across the gate: the per-tick `commitHash` stream
//       and the final `WorldSnapshot` hash match between
//       `workloadAwareCommit=false` and `=true` on both workload
//       shapes. The gate is a scheduling knob; world state is
//       unchanged.
//
// HIGH-GLOBAL shape — every command is a tag flip (CmdAddTag /
// CmdRemoveTag), so `valueOnlyCount == 0` and `globalCount ==
// totalCommands`. That trips the pre-existing `totalValueOnly == 0`
// fallthrough on EVERY call, so the S16 gate would never fire — we
// instead build a MIXED-HEAVY shape where ~50% of commands are global
// and ~50% are value-only. That lands on the RPG-mix side of the
// threshold.
//
// LOW-GLOBAL shape — every command is `setTransform` (100 % value-
// only), the canonical sharded fast-path.
//
// `kShardedMinCommands = 256` is a private internal constant; we pad
// the entity count high enough that every step easily clears it.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace threadmaxx;

// ---------------------------------------------------------------------
// FNV-1a-64 over a WorldSnapshot's serialized bytes (mirrors the
// reference helper used in sharded_commit_test.cpp).
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

// HIGH-GLOBAL system: alternates setTransform (value-only) and a
// tag flip per entity. globalCount / totalCommands ≈ 0.5, comfortably
// over the default 30 % threshold.
class HighGlobalSystem : public ISystem {
public:
    explicit HighGlobalSystem(std::vector<EntityHandle> entities)
        : entities_(std::move(entities)) {}
    const char* name() const noexcept override { return "high-global"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::all(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        const std::uint64_t t = ctx.tick();
        ctx.single([this, t](Range, CommandBuffer& cb) {
            const std::size_t n = entities_.size();
            for (std::size_t i = 0; i < n; ++i) {
                Transform tr{};
                tr.position.x = static_cast<float>((i + t) & 0xFF);
                cb.setTransform(entities_[i], tr);
                if ((i + t) % 2 == 0) {
                    cb.addTag(entities_[i], Component::StaticTag);
                } else {
                    cb.removeTag(entities_[i], Component::StaticTag);
                }
            }
        });
    }
private:
    std::vector<EntityHandle> entities_;
};

// LOW-GLOBAL system: pure setTransform (100 % value-only).
class LowGlobalSystem : public ISystem {
public:
    explicit LowGlobalSystem(std::vector<EntityHandle> entities)
        : entities_(std::move(entities)) {}
    const char* name() const noexcept override { return "low-global"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::all(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        const std::uint64_t t = ctx.tick();
        ctx.single([this, t](Range, CommandBuffer& cb) {
            const std::size_t n = entities_.size();
            for (std::size_t i = 0; i < n; ++i) {
                Transform tr{};
                tr.position.x = static_cast<float>((i + t) & 0xFF);
                tr.position.y = static_cast<float>((i * 3 + t) & 0xFF);
                cb.setTransform(entities_[i], tr);
            }
        });
    }
private:
    std::vector<EntityHandle> entities_;
};

struct SeedGame : IGame {
    std::size_t count = 0;
    void onSetup(Engine&, World&, CommandBuffer& cb) override {
        for (std::size_t i = 0; i < count; ++i) {
            Transform t{};
            t.position.x = static_cast<float>(i);
            // Spawn across two archetypes so the sharded path's
            // `chunkCount < 2` guard is cleared.
            if (i & 1) {
                cb.spawn(t, Velocity{}, RenderTag{},
                         UserData{static_cast<std::uint64_t>(i)});
            } else {
                Bundle b;
                b.transform   = t;
                b.health      = Health{};
                b.initialMask = ComponentSet{Component::Transform} |
                                ComponentSet{Component::Velocity} |
                                ComponentSet{Component::UserData} |
                                ComponentSet{Component::Acceleration} |
                                ComponentSet{Component::Health};
                cb.spawnBundle(b);
            }
        }
    }
};

struct RunResult {
    std::vector<std::uint64_t> tickHashes;
    std::uint64_t              snapshotHash = 0;
    std::uint64_t              shardedCalls = 0;
    std::uint64_t              fallbackCalls = 0;
    std::uint64_t              workloadAwareFallthrough = 0;
};

enum class Workload { HighGlobal, LowGlobal };

constexpr int kTicks = 64;
constexpr std::size_t kEntities = 1024;

RunResult runScenario(Workload w, bool workloadAware) {
    Config cfg;
    cfg.sleepToPace          = false;
    cfg.workerCount          = 4;
    cfg.deterministic        = true;
    cfg.singleThreadedCommit = false;
    cfg.workloadAwareCommit  = workloadAware;
    Engine engine(cfg);

    SeedGame game;
    game.count = kEntities;
    CHECK(engine.initialize(game));

    std::vector<EntityHandle> seeded;
    {
        const auto span = engine.world().entities();
        seeded.assign(span.begin(), span.end());
    }
    CHECK_EQ(seeded.size(), kEntities);

    if (w == Workload::HighGlobal) {
        engine.registerSystem(std::make_unique<HighGlobalSystem>(seeded));
    } else {
        engine.registerSystem(std::make_unique<LowGlobalSystem>(seeded));
    }

    RunResult out;
    out.tickHashes.reserve(kTicks);
    for (int i = 0; i < kTicks; ++i) {
        engine.step();
        out.tickHashes.push_back(engine.stats().commitHash);
        const auto bd = engine.lastCommitBreakdown();
        out.shardedCalls             += bd.shardedCalls;
        out.fallbackCalls            += bd.fallbackCalls;
        out.workloadAwareFallthrough += bd.workloadAwareFallthrough;
    }

    out.snapshotHash = snapshotHash(engine.world().snapshot());
    engine.shutdown();
    return out;
}

} // namespace

int main() {
    // (1) HIGH-GLOBAL + workloadAwareCommit=true → fallthrough fires.
    //     Every step's commit call has globalCount/totalCommands ≈ 0.5,
    //     trips the 30 % threshold, falls through to serial.
    const auto highOn = runScenario(Workload::HighGlobal, /*workloadAware=*/true);
    CHECK(highOn.workloadAwareFallthrough > 0);
    CHECK_EQ(highOn.shardedCalls, 0ull);
    // `fallbackCalls` counts ALL fallthrough cases; on a high-global
    // workload it must be ≥ workloadAwareFallthrough.
    CHECK(highOn.fallbackCalls >= highOn.workloadAwareFallthrough);

    // (2) LOW-GLOBAL + workloadAwareCommit=true → sharded runs.
    //     Every step's commit has globalCount == 0, never trips S16.
    //     The pre-existing `totalValueOnly == 0` guard also doesn't
    //     trigger (it would require globalCount == totalCommands).
    const auto lowOn = runScenario(Workload::LowGlobal, /*workloadAware=*/true);
    CHECK_EQ(lowOn.workloadAwareFallthrough, 0ull);
    CHECK(lowOn.shardedCalls > 0);

    // (3) Determinism — the gate is a scheduling knob, world state is
    //     unchanged. Both workloads must produce identical commitHash
    //     stream and snapshot hash with the knob ON vs OFF.
    const auto highOff = runScenario(Workload::HighGlobal, /*workloadAware=*/false);
    const auto lowOff  = runScenario(Workload::LowGlobal,  /*workloadAware=*/false);

    CHECK_EQ(highOn.tickHashes.size(), highOff.tickHashes.size());
    for (std::size_t t = 0; t < highOn.tickHashes.size(); ++t) {
        if (highOn.tickHashes[t] != highOff.tickHashes[t]) {
            std::fprintf(stderr,
                "high-global tick=%zu on=0x%016llx off=0x%016llx\n",
                t,
                (unsigned long long)highOn.tickHashes[t],
                (unsigned long long)highOff.tickHashes[t]);
        }
        CHECK_EQ(highOn.tickHashes[t], highOff.tickHashes[t]);
    }
    CHECK_EQ(highOn.snapshotHash, highOff.snapshotHash);

    CHECK_EQ(lowOn.tickHashes.size(), lowOff.tickHashes.size());
    for (std::size_t t = 0; t < lowOn.tickHashes.size(); ++t) {
        if (lowOn.tickHashes[t] != lowOff.tickHashes[t]) {
            std::fprintf(stderr,
                "low-global tick=%zu on=0x%016llx off=0x%016llx\n",
                t,
                (unsigned long long)lowOn.tickHashes[t],
                (unsigned long long)lowOff.tickHashes[t]);
        }
        CHECK_EQ(lowOn.tickHashes[t], lowOff.tickHashes[t]);
    }
    CHECK_EQ(lowOn.snapshotHash, lowOff.snapshotHash);

    return 0;
}
