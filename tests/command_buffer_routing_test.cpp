// SHARDED_OPTIMISATION.md S8 — record-time per-chunk routing correctness.
//
// Each CommandBuffer carries `chunkBuckets()` and `globalIndices()`
// auxiliary index lists populated at record time when an engine-
// installed locator hook is present. The sharded commit consumes
// those lists to skip the per-command `locate()` in Pass B.
//
// Unit-level tests use a fake locator on a bare `CommandBuffer` to
// assert the recording-side classification. End-to-end tests drive
// a real `Engine` in both serial and sharded modes and compare the
// commitHash stream — the determinism contract is bit-exact.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace {

using namespace threadmaxx;

// ---------------------------------------------------------------------
// Unit-level: CommandBuffer recording-side classification.

void test_empty_buffer_no_routing_state() {
    CommandBuffer cb;
    CHECK(cb.empty());
    CHECK(cb.commands().empty());
    CHECK_EQ(cb.globalIndices().size(), std::size_t{0});
    CHECK_EQ(cb.chunkBuckets().size(),  std::size_t{0});
    CHECK(!cb.routingActive());
}

void test_routing_classifies_value_only_and_migrating() {
    static const std::uint32_t kArchA = 3;
    auto locator = [](const void*, EntityHandle h) noexcept -> std::uint32_t {
        if (!h.valid()) return CommandBuffer::kInvalidArchetype;
        return kArchA;
    };
    CommandBuffer cb;
    cb.setLocator(locator, nullptr);
    CHECK(cb.routingActive());

    EntityHandle e;
    e.index      = 7;
    e.generation = 1;

    cb.setTransform(e, Transform{});       // value-only → bucket A
    cb.addTag(e, Component::StaticTag);    // migrating  → global
    cb.setVelocity(e, Velocity{});         // value-only → bucket A
    cb.removeTag(e, Component::StaticTag); // migrating  → global

    CHECK_EQ(cb.commands().size(), std::size_t{4});
    CHECK_EQ(cb.globalIndices().size(), std::size_t{2});
    CHECK(cb.chunkBuckets().size() > kArchA);
    CHECK_EQ(cb.chunkBuckets()[kArchA].size(), std::size_t{2});
    // global indices = 1 (addTag), 3 (removeTag); bucket = 0, 2.
    CHECK_EQ(cb.globalIndices()[0], std::uint32_t{1});
    CHECK_EQ(cb.globalIndices()[1], std::uint32_t{3});
    CHECK_EQ(cb.chunkBuckets()[kArchA][0], std::uint32_t{0});
    CHECK_EQ(cb.chunkBuckets()[kArchA][1], std::uint32_t{2});
}

void test_stale_locator_routes_to_global() {
    auto locator = [](const void*, EntityHandle) noexcept -> std::uint32_t {
        return CommandBuffer::kInvalidArchetype;
    };
    CommandBuffer cb;
    cb.setLocator(locator, nullptr);
    EntityHandle e;
    e.index = 1;
    e.generation = 1;
    cb.setTransform(e, Transform{});
    cb.setVelocity(e, Velocity{});
    CHECK_EQ(cb.globalIndices().size(), std::size_t{2});
    CHECK(cb.chunkBuckets().empty());
}

void test_clear_resets_routing_state() {
    static const std::uint32_t kArchA = 0;
    auto locator = [](const void*, EntityHandle) noexcept -> std::uint32_t {
        return kArchA;
    };
    CommandBuffer cb;
    cb.setLocator(locator, nullptr);
    EntityHandle e;
    e.index = 2; e.generation = 1;
    cb.setTransform(e, Transform{});
    cb.addTag(e, Component::StaticTag);
    cb.clear();
    CHECK(cb.empty());
    CHECK_EQ(cb.globalIndices().size(), std::size_t{0});
    for (const auto& b : cb.chunkBuckets()) CHECK(b.empty());
    // Locator must persist across clear so the next wave can re-use
    // the same buffer.
    CHECK(cb.routingActive());
}

void test_no_locator_skips_routing() {
    // Default-constructed CommandBuffer has no locator. Recording
    // must NOT populate buckets / globalIdx — that's the zero-cost
    // single-threaded commit fast path.
    CommandBuffer cb;
    EntityHandle e;
    e.index = 3; e.generation = 1;
    cb.setTransform(e, Transform{});
    cb.addTag(e, Component::StaticTag);
    CHECK_EQ(cb.commands().size(), std::size_t{2});
    CHECK(cb.globalIndices().empty());
    CHECK(cb.chunkBuckets().empty());
}

// ---------------------------------------------------------------------
// End-to-end determinism: serial vs sharded must produce identical
// commitHash streams across a mutation script that mixes spawn /
// addTag / removeTag / setTransform / setVelocity.

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

std::vector<std::uint64_t>
runScript(bool sharded, std::uint32_t entityCount, std::uint32_t ticks,
          const ScriptedMutator::Fn& script) {
    Config cfg;
    cfg.fixedStepSeconds      = 1.0/60.0;
    cfg.initialEntityCapacity = entityCount;
    cfg.workerCount           = 4;
    cfg.singleThreadedCommit  = !sharded;
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

void test_determinism_serial_vs_sharded_pure_value_only() {
    // All cmds are value-only setTransform — entirely bucketed under
    // S8; global lane is empty. Pass A skip path.
    auto script = [](CommandBuffer& cb, std::uint64_t tick,
                     std::span<const EntityHandle> targets) {
        for (auto e : targets) {
            Transform t{};
            t.position = Vec3{static_cast<float>(e.index),
                              static_cast<float>(tick),
                              0.f};
            cb.setTransform(e, t);
        }
    };
    auto serial  = runScript(false, 384, 20, script);
    auto sharded = runScript(true,  384, 20, script);
    CHECK_EQ(serial.size(),  std::size_t{20});
    CHECK_EQ(sharded.size(), std::size_t{20});
    for (std::size_t i = 0; i < serial.size(); ++i) {
        CHECK_EQ(serial[i], sharded[i]);
    }
}

void test_determinism_serial_vs_sharded_value_with_migration() {
    // Mixed: setTransform + addTag/removeTag churn on a subset of
    // entities. The post-tag setTransform on the same entity must
    // demote via the wave-cumulative migrating bitmap and merge-apply
    // in submission order.
    auto script = [](CommandBuffer& cb, std::uint64_t tick,
                     std::span<const EntityHandle> targets) {
        const std::size_t half = targets.size() / 2;
        for (std::size_t i = 0; i < targets.size(); ++i) {
            const auto e = targets[i];
            Transform t1{};
            t1.position = Vec3{static_cast<float>(i),
                               static_cast<float>(tick),
                               0.f};
            cb.setTransform(e, t1);
            if (i < half) {
                if (tick % 2 == 0) cb.addTag(e, Component::StaticTag);
                else               cb.removeTag(e, Component::StaticTag);
                // Second value-only AFTER the migration — must demote
                // under S8 because the entity is now in the migrating
                // bitmap.
                Transform t2{};
                t2.position = Vec3{0.f,
                                   static_cast<float>(i),
                                   static_cast<float>(tick)};
                cb.setTransform(e, t2);
            }
        }
    };
    auto serial  = runScript(false, 384, 20, script);
    auto sharded = runScript(true,  384, 20, script);
    CHECK_EQ(serial.size(),  std::size_t{20});
    CHECK_EQ(sharded.size(), std::size_t{20});
    for (std::size_t i = 0; i < serial.size(); ++i) {
        CHECK_EQ(serial[i], sharded[i]);
    }
}

void test_determinism_serial_vs_sharded_migration_only() {
    // All cmds are migrating (addTag / removeTag alternating). The
    // S8 fast path: buckets are empty, every cmd is in globalIdx_,
    // the merge degenerates to plain global-lane application with
    // the migration-batch run detector intact.
    auto script = [](CommandBuffer& cb, std::uint64_t tick,
                     std::span<const EntityHandle> targets) {
        const Component tag = Component::StaticTag;
        for (auto e : targets) {
            if (tick % 2 == 0) cb.addTag(e, tag);
            else               cb.removeTag(e, tag);
        }
    };
    auto serial  = runScript(false, 384, 20, script);
    auto sharded = runScript(true,  384, 20, script);
    for (std::size_t i = 0; i < serial.size(); ++i) {
        CHECK_EQ(serial[i], sharded[i]);
    }
}

} // namespace

int main() {
    test_empty_buffer_no_routing_state();
    test_routing_classifies_value_only_and_migrating();
    test_stale_locator_routes_to_global();
    test_clear_resets_routing_state();
    test_no_locator_skips_routing();
    test_determinism_serial_vs_sharded_pure_value_only();
    test_determinism_serial_vs_sharded_value_with_migration();
    test_determinism_serial_vs_sharded_migration_only();
    EXIT_WITH_RESULT();
}
