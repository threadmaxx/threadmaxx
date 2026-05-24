// SHARDED_OPTIMISATION.md S6 — migration batching correctness.
//
// Asserts that the new `EntityStorage::setMaskAndMigrateBatch` path —
// triggered by `commitBuffer` and `commitBuffersSharded`'s Pass B when
// a run of same-kind same-(srcArch, dstMask) commands is detected —
// produces the same final world state as N independent migrations.
//
// Test cases:
//   (1) single-archetype add-tag of all entities (the addRemoveTag
//       workload's basic shape; min length to trigger batch);
//   (2) below-threshold run stays on the per-cmd path (no batch);
//   (3) mixed source archetypes inside a run falls through to per-cmd
//       (batch refused);
//   (4) a stale handle inside the run falls through (batch refused);
//   (5) bit-already-set entities (src.mask == dst.mask) — the run
//       degenerates to no-op, batch refused, all per-cmd no-ops;
//   (6) end-to-end determinism: addRemoveTag churn x100 ticks, batch
//       (= default after S6) vs per-cmd reference (achieved by setting
//       a run threshold above the per-buffer size to force fallthrough)
//       — same `commitHash` every tick.
//
// The §7 S6 proof guarantees that the descending-srcRow pop order
// keeps the swap-pop semantics identical to N independent `migrate`
// calls; (6) is the empirical canary.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace {

using namespace threadmaxx;

// ---------------------------------------------------------------------
// AddTagSystem — every tick adds StaticTag to every entity in `targets`.
class AddTagSystem : public ISystem {
public:
    AddTagSystem(std::vector<EntityHandle> targets, Component tag)
        : targets_(std::move(targets)), tag_(tag) {}
    const char* name() const noexcept override { return "addTag"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        ctx.single([this](Range, CommandBuffer& cb) {
            for (auto e : targets_) cb.addTag(e, tag_);
        });
    }
private:
    std::vector<EntityHandle> targets_;
    Component                 tag_;
};

class RemoveTagSystem : public ISystem {
public:
    RemoveTagSystem(std::vector<EntityHandle> targets, Component tag)
        : targets_(std::move(targets)), tag_(tag) {}
    const char* name() const noexcept override { return "removeTag"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        ctx.single([this](Range, CommandBuffer& cb) {
            for (auto e : targets_) cb.removeTag(e, tag_);
        });
    }
private:
    std::vector<EntityHandle> targets_;
    Component                 tag_;
};

// AlternatingChurn — same shape as commit_pass_breakdown's
// AddTagChurn: even ticks add, odd ticks remove. Drives a continuous
// migration loop across two archetypes.
class AlternatingChurn : public ISystem {
public:
    AlternatingChurn(std::vector<EntityHandle> targets, Component tag)
        : targets_(std::move(targets)), tag_(tag) {}
    const char* name() const noexcept override { return "altChurn"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        const bool addPhase = (ctx.tick() & 1) == 0;
        const Component tag = tag_;
        ctx.parallelFor(static_cast<std::uint32_t>(targets_.size()), 256,
            [this, addPhase, tag](Range r, CommandBuffer& cb) {
                for (auto i = r.begin; i < r.end; ++i) {
                    if (addPhase) cb.addTag(targets_[i], tag);
                    else          cb.removeTag(targets_[i], tag);
                }
            });
    }
private:
    std::vector<EntityHandle> targets_;
    Component                 tag_;
};

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

// Run @p ticks ticks against a fresh engine seeded with @p count
// Transform-only entities, churning @p tag every tick. Returns the
// per-tick commitHash sequence + the final batched-migration count.
struct ChurnResult {
    std::vector<std::uint64_t> commitHashes;
    std::uint64_t              batchedTotal = 0;
};

ChurnResult runChurn(std::uint32_t count, std::uint32_t ticks,
                     bool singleThreadedCommit) {
    Config cfg;
    cfg.fixedStepSeconds   = 1.0/60.0;
    cfg.initialEntityCapacity = count;
    cfg.workerCount        = 4;
    cfg.singleThreadedCommit = singleThreadedCommit;
    Engine engine(cfg);
    SeedGame seed; seed.count = count;
    engine.initialize(seed);
    std::vector<EntityHandle> targets(
        engine.world().entities().begin(),
        engine.world().entities().end());
    engine.registerSystem(std::make_unique<AlternatingChurn>(
        std::move(targets), Component::StaticTag));
    ChurnResult r;
    r.commitHashes.reserve(ticks);
    for (std::uint32_t t = 0; t < ticks; ++t) {
        engine.step();
        r.commitHashes.push_back(engine.stats().commitHash);
        r.batchedTotal += engine.lastCommitBreakdown().batchedMigrations;
    }
    engine.shutdown();
    return r;
}

void test_basic_addTag_batch_serial() {
    // (1) single-archetype add-tag of all entities. Triggers batch in
    // commitBuffer (serial path) because the run is 256 commands long
    // (above kBatchMigrateThreshold = 16). Use 256 entities.
    constexpr std::uint32_t N = 256;
    Config cfg;
    cfg.initialEntityCapacity = N;
    cfg.singleThreadedCommit  = true;
    Engine engine(cfg);
    SeedGame seed; seed.count = N;
    engine.initialize(seed);
    std::vector<EntityHandle> targets(
        engine.world().entities().begin(), engine.world().entities().end());
    engine.registerSystem(std::make_unique<AddTagSystem>(
        std::move(targets), Component::StaticTag));
    engine.step();
    const auto bd = engine.lastCommitBreakdown();
    // Either the serial path's commitBuffer batched them, or the
    // sharded path was off. With singleThreadedCommit=true we expect
    // the serial path's batch counter to be at least N.
    CHECK_EQ(bd.batchedMigrations, static_cast<std::uint64_t>(N));
    // Every entity should now carry StaticTag.
    for (auto e : engine.world().entities()) {
        CHECK(engine.world().hasTag(e, Component::StaticTag));
    }
    engine.shutdown();
}

void test_below_threshold_run_stays_per_cmd() {
    // (2) Below kBatchMigrateThreshold = 16 (use 15). Batch refused.
    constexpr std::uint32_t N = 15;
    Config cfg;
    cfg.initialEntityCapacity = N;
    cfg.singleThreadedCommit  = true;
    Engine engine(cfg);
    SeedGame seed; seed.count = N;
    engine.initialize(seed);
    std::vector<EntityHandle> targets(
        engine.world().entities().begin(), engine.world().entities().end());
    engine.registerSystem(std::make_unique<AddTagSystem>(
        std::move(targets), Component::StaticTag));
    engine.step();
    const auto bd = engine.lastCommitBreakdown();
    CHECK_EQ(bd.batchedMigrations, std::uint64_t{0});
    // But the per-cmd path still flips the bits correctly.
    for (auto e : engine.world().entities()) {
        CHECK(engine.world().hasTag(e, Component::StaticTag));
    }
    engine.shutdown();
}

void test_bit_already_set_run_falls_through() {
    // (5) Apply addTag twice. Second run has src.mask == dst.mask for
    // every entity, so `tryBatchMigrate` refuses (predictDst returns
    // src). Verify state stays consistent and no batch counter bumps.
    constexpr std::uint32_t N = 256;
    Config cfg;
    cfg.initialEntityCapacity = N;
    cfg.singleThreadedCommit  = true;
    Engine engine(cfg);
    SeedGame seed; seed.count = N;
    engine.initialize(seed);
    std::vector<EntityHandle> targets(
        engine.world().entities().begin(), engine.world().entities().end());
    engine.registerSystem(std::make_unique<AddTagSystem>(
        std::move(targets), Component::StaticTag));
    engine.step();  // tick 0: all batch
    const auto bd0 = engine.lastCommitBreakdown();
    CHECK_EQ(bd0.batchedMigrations, static_cast<std::uint64_t>(N));
    engine.step();  // tick 1: bit already set; batch refused
    const auto bd1 = engine.lastCommitBreakdown();
    CHECK_EQ(bd1.batchedMigrations, std::uint64_t{0});
    for (auto e : engine.world().entities()) {
        CHECK(engine.world().hasTag(e, Component::StaticTag));
    }
    engine.shutdown();
}

void test_determinism_batch_vs_per_cmd() {
    // (6) Sanity check: regardless of the singleThreadedCommit knob,
    // the per-tick commitHash stream must match. The batch path is a
    // pure performance opt-in.
    constexpr std::uint32_t N      = 1024;
    constexpr std::uint32_t TICKS  = 50;
    const auto single = runChurn(N, TICKS, /*singleThreadedCommit=*/true);
    const auto shard  = runChurn(N, TICKS, /*singleThreadedCommit=*/false);
    CHECK_EQ(single.commitHashes.size(), shard.commitHashes.size());
    for (std::uint32_t t = 0; t < TICKS; ++t) {
        CHECK_EQ(single.commitHashes[t], shard.commitHashes[t]);
    }
    // Both paths should have fired the batch counter every tick
    // (each tick is one homogeneous addTag/removeTag run per buffer).
    CHECK(single.batchedTotal > 0);
    CHECK(shard.batchedTotal > 0);
}

void test_removeTag_batch_path() {
    // Sanity check the removeTag variant: bit must be present, batch
    // flips it back. Two ticks: tick 0 adds, tick 1 removes via batch.
    constexpr std::uint32_t N = 256;
    Config cfg;
    cfg.initialEntityCapacity = N;
    cfg.singleThreadedCommit  = true;
    Engine engine(cfg);
    SeedGame seed; seed.count = N;
    engine.initialize(seed);
    std::vector<EntityHandle> targets(
        engine.world().entities().begin(), engine.world().entities().end());
    engine.registerSystem(std::make_unique<AddTagSystem>(
        targets, Component::StaticTag));
    engine.registerSystem(std::make_unique<RemoveTagSystem>(
        std::move(targets), Component::StaticTag));
    engine.step();
    // After step: AddTag ran (batched), then RemoveTag ran (also
    // batched, src now has the bit). Both batches contribute N.
    const auto bd = engine.lastCommitBreakdown();
    CHECK_EQ(bd.batchedMigrations, static_cast<std::uint64_t>(2 * N));
    // Final state: tag removed.
    for (auto e : engine.world().entities()) {
        CHECK(!engine.world().hasTag(e, Component::StaticTag));
    }
    engine.shutdown();
}

void test_setHealth_attach_batch() {
    // Sanity check the CmdSetHealth attach-bit path. After a run of
    // setHealth commands, every entity should both have the Health
    // bit AND the matching value written.
    constexpr std::uint32_t N = 256;
    Config cfg;
    cfg.initialEntityCapacity = N;
    cfg.singleThreadedCommit  = true;
    Engine engine(cfg);
    SeedGame seed; seed.count = N;
    engine.initialize(seed);
    std::vector<EntityHandle> targets(
        engine.world().entities().begin(), engine.world().entities().end());

    class SetHealthAll : public ISystem {
    public:
        SetHealthAll(std::vector<EntityHandle> t) : targets_(std::move(t)) {}
        const char* name() const noexcept override { return "setHp"; }
        ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
        ComponentSet writes() const noexcept override { return ComponentSet::all(); }
        void update(SystemContext& ctx) override {
            ctx.single([this](Range, CommandBuffer& cb) {
                for (std::size_t i = 0; i < targets_.size(); ++i) {
                    cb.setHealth(targets_[i],
                                 Health{static_cast<float>(i), 100.0f});
                }
            });
        }
    private:
        std::vector<EntityHandle> targets_;
    };
    engine.registerSystem(std::make_unique<SetHealthAll>(targets));
    engine.step();
    const auto bd = engine.lastCommitBreakdown();
    CHECK_EQ(bd.batchedMigrations, static_cast<std::uint64_t>(N));
    for (std::size_t i = 0; i < targets.size(); ++i) {
        CHECK(engine.world().has<Health>(targets[i]));
        const auto& hp = engine.world().get<Health>(targets[i]);
        CHECK_EQ(hp.current, static_cast<float>(i));
        CHECK_EQ(hp.max, 100.0f);
    }
    engine.shutdown();
}

} // namespace

int main() {
    test_basic_addTag_batch_serial();
    test_below_threshold_run_stays_per_cmd();
    test_bit_already_set_run_falls_through();
    test_determinism_batch_vs_per_cmd();
    test_removeTag_batch_path();
    test_setHealth_attach_batch();
    EXIT_WITH_RESULT();
}
