// SHARDED_OPTIMISATION.md S10 — row-split largest bin determinism.
//
// Pass C splits the single largest large-bin into row-range sub-bins
// when `Config::splitLargestBin` is set and the bin exceeds
// `2 * kMinBinForJob` cmds. Sub-bin 0 runs inline on the sim thread,
// sub-bins [1..M) on workers. Each cmd routes to the sub-bin whose
// row range covers its target entity's current row.
//
// Correctness contract: bit-exact commitHash stream against the
// pre-S10 path (`splitLargestBin = false`) and against the fully
// serial baseline. The bench worth measuring this against is
// `bench/commit_pass_breakdown` with the `THREADMAXX_NO_SPLIT_LARGEST`
// env var; this test pins the hash.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace {

using namespace threadmaxx;

struct SeedGame : IGame {
    std::uint32_t count = 0;
    void onSetup(Engine&, World&, CommandBuffer& cb) override {
        for (std::uint32_t i = 0; i < count; ++i) {
            Transform tr{};
            tr.position.x = static_cast<float>(i);
            cb.spawn(tr);
        }
    }
};

class ScriptedMutator : public ISystem {
public:
    using Fn = std::function<void(CommandBuffer&, std::uint64_t tick,
                                  std::span<const EntityHandle>)>;
    ScriptedMutator(std::vector<EntityHandle> targets, Fn fn)
        : targets_(std::move(targets)), fn_(std::move(fn)) {}
    const char* name() const noexcept override { return "scriptedMutator"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        ctx.single([this, &ctx](Range, CommandBuffer& cb) {
            fn_(cb, ctx.tick(), targets_);
        });
    }
private:
    std::vector<EntityHandle>     targets_;
    Fn                            fn_;
};

enum class Mode { Serial, ShardedNoSplit, ShardedSplit };

std::vector<std::uint64_t>
runScript(Mode mode, std::uint32_t entityCount, std::uint32_t ticks,
          const ScriptedMutator::Fn& script) {
    Config cfg;
    cfg.fixedStepSeconds      = 1.0/60.0;
    cfg.initialEntityCapacity = entityCount;
    cfg.workerCount           = 4;
    cfg.singleThreadedCommit  = (mode == Mode::Serial);
    cfg.splitLargestBin       = (mode == Mode::ShardedSplit);
    Engine engine(cfg);
    SeedGame seed;
    seed.count = entityCount;
    engine.initialize(seed);
    std::vector<EntityHandle> targets(
        engine.world().entities().begin(), engine.world().entities().end());
    engine.registerSystem(std::make_unique<ScriptedMutator>(
        std::move(targets), script));
    std::vector<std::uint64_t> out;
    out.reserve(ticks);
    for (std::uint32_t t = 0; t < ticks; ++t) {
        engine.step();
        out.push_back(engine.stats().commitHash);
    }
    engine.shutdown();
    return out;
}

// 4096 entities × 60 ticks of setTransform → single bin of 4096
// cmds per tick (largeBins = 1). With workerCount=4, splitFactor
// = 4 - 1 + 2 = 5; bin / kMinBinForJob (256) = 16, so splitFactor
// clamps to 5. Sub-bins 0..4, sim takes 0, workers take 1..4.
void test_pure_value_only_split_vs_baseline() {
    auto script = [](CommandBuffer& cb, std::uint64_t tick,
                     std::span<const EntityHandle> targets) {
        for (std::size_t i = 0; i < targets.size(); ++i) {
            Transform t{};
            t.position = Vec3{static_cast<float>(i),
                              static_cast<float>(tick),
                              0.f};
            cb.setTransform(targets[i], t);
        }
    };
    auto serial   = runScript(Mode::Serial,         4096, 60, script);
    auto noSplit  = runScript(Mode::ShardedNoSplit, 4096, 60, script);
    auto split    = runScript(Mode::ShardedSplit,   4096, 60, script);
    CHECK_EQ(serial.size(),  std::size_t{60});
    CHECK_EQ(noSplit.size(), std::size_t{60});
    CHECK_EQ(split.size(),   std::size_t{60});
    for (std::size_t i = 0; i < serial.size(); ++i) {
        CHECK_EQ(serial[i],  noSplit[i]);
        CHECK_EQ(noSplit[i], split[i]);
    }
}

// Multi-component cmds on the same large bin (setTransform +
// setVelocity). Each cmd's row lookup must classify identically so
// that two cmds on the same entity land in the same sub-bin and
// execute in submission order.
void test_multi_setter_split_vs_baseline() {
    auto script = [](CommandBuffer& cb, std::uint64_t tick,
                     std::span<const EntityHandle> targets) {
        for (std::size_t i = 0; i < targets.size(); ++i) {
            Transform t{};
            t.position = Vec3{static_cast<float>(i),
                              static_cast<float>(tick), 0.f};
            cb.setTransform(targets[i], t);
            Velocity v{};
            v.linear = Vec3{static_cast<float>(tick),
                            static_cast<float>(i), 0.f};
            cb.setVelocity(targets[i], v);
            // Second setTransform on same entity — must follow the
            // first in submission order within the same sub-bin.
            Transform t2{};
            t2.position = Vec3{1.f,
                               static_cast<float>(tick),
                               static_cast<float>(i)};
            cb.setTransform(targets[i], t2);
        }
    };
    auto serial   = runScript(Mode::Serial,         3072, 40, script);
    auto noSplit  = runScript(Mode::ShardedNoSplit, 3072, 40, script);
    auto split    = runScript(Mode::ShardedSplit,   3072, 40, script);
    for (std::size_t i = 0; i < serial.size(); ++i) {
        CHECK_EQ(serial[i],  noSplit[i]);
        CHECK_EQ(noSplit[i], split[i]);
    }
}

// Mix migration cmds with value-only cmds — entity goes through
// addTag (migrate) → setTransform (value-only). The post-migrate
// value-only cmd must demote via the wave-cumulative migrating
// bitmap; the remaining value-only cmds populate the largest bin
// which then row-splits.
void test_migration_plus_split_vs_baseline() {
    auto script = [](CommandBuffer& cb, std::uint64_t tick,
                     std::span<const EntityHandle> targets) {
        const std::size_t half = targets.size() / 2;
        for (std::size_t i = 0; i < targets.size(); ++i) {
            const auto e = targets[i];
            Transform t1{};
            t1.position = Vec3{static_cast<float>(i),
                               static_cast<float>(tick), 0.f};
            cb.setTransform(e, t1);
            if (i < half) {
                if (tick % 2 == 0) cb.addTag(e, Component::StaticTag);
                else               cb.removeTag(e, Component::StaticTag);
                Transform t2{};
                t2.position = Vec3{0.f,
                                   static_cast<float>(i),
                                   static_cast<float>(tick)};
                cb.setTransform(e, t2);
            }
        }
    };
    auto serial   = runScript(Mode::Serial,         2048, 30, script);
    auto noSplit  = runScript(Mode::ShardedNoSplit, 2048, 30, script);
    auto split    = runScript(Mode::ShardedSplit,   2048, 30, script);
    for (std::size_t i = 0; i < serial.size(); ++i) {
        CHECK_EQ(serial[i],  noSplit[i]);
        CHECK_EQ(noSplit[i], split[i]);
    }
}

// Bin below the 2 * kMinBinForJob eligibility threshold — split
// should NOT fire (splitFactor stays 1, falls through to S9). The
// hash must still match the no-split sharded path AND the serial
// baseline.
void test_small_bin_below_threshold_no_split() {
    // 384 cmds < 2 * 256 → ineligible; should mirror noSplit path
    // exactly even with splitLargestBin = true.
    auto script = [](CommandBuffer& cb, std::uint64_t tick,
                     std::span<const EntityHandle> targets) {
        for (std::size_t i = 0; i < targets.size(); ++i) {
            Transform t{};
            t.position = Vec3{static_cast<float>(i),
                              static_cast<float>(tick), 0.f};
            cb.setTransform(targets[i], t);
        }
    };
    auto serial   = runScript(Mode::Serial,          384, 30, script);
    auto noSplit  = runScript(Mode::ShardedNoSplit,  384, 30, script);
    auto split    = runScript(Mode::ShardedSplit,    384, 30, script);
    for (std::size_t i = 0; i < serial.size(); ++i) {
        CHECK_EQ(serial[i],  noSplit[i]);
        CHECK_EQ(noSplit[i], split[i]);
    }
}

} // namespace

int main() {
    test_pure_value_only_split_vs_baseline();
    test_multi_setter_split_vs_baseline();
    test_migration_plus_split_vs_baseline();
    test_small_bin_below_threshold_no_split();
    EXIT_WITH_RESULT();
}
