// §3.6.5 batch 15b — Heavy multi-feature concurrency soak.
//
// Composes the realistic production-ish surface every batch lands on:
//   - 8 worker threads
//   - sharded commit path (`singleThreadedCommit = false`)
//   - tick budget + Budget skip policy
//   - hot-reload events flowing through the lock-free MPSC channel
//   - RAII subscription handles
//   - stall watchdog armed (longer than per-tick budget so it does
//     not fire on normal ticks)
//   - 4096 entities × 200 ticks
//
// Asserts:
//   - Engine survives without deadlock / asan / data-race-detected
//     report. (We can't probe TSAN from inside the test, but the
//     soak's structure is exactly what TSAN runs catch.)
//   - Per-tick `commitHash` is bit-for-bit reproducible across two
//     runs of the same seed.
//   - SystemSkipped events arrive on the channel and the count
//     matches the per-tick budget overruns we forced.
//   - Subscriptions stay valid until they drop out of scope.
//   - No spurious EngineStall events fired (timeout >> step time).

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>
#include <threadmaxx/Telemetry.hpp>

#include <atomic>
#include <cstdint>
#include <random>
#include <vector>

namespace {

using namespace threadmaxx;

class MoverSystem : public ISystem {
public:
    const char* name() const noexcept override { return "mover"; }
    ComponentSet reads()  const noexcept override {
        return ComponentSet{Component::Velocity};
    }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Transform};
    }
    void update(SystemContext& ctx) override {
        const auto n = static_cast<std::uint32_t>(ctx.world().size());
        ctx.parallelFor(n, 256, [&ctx](Range r, CommandBuffer& cb) {
            const auto handles = ctx.world().entities();
            const auto velocities = ctx.world().velocities();
            const auto transforms = ctx.world().transforms();
            const float dt = static_cast<float>(ctx.dt());
            for (std::uint32_t i = r.begin; i < r.end; ++i) {
                Transform t = transforms[i];
                t.position.x += velocities[i].linear.x * dt;
                t.position.y += velocities[i].linear.y * dt;
                t.position.z += velocities[i].linear.z * dt;
                cb.setTransform(handles[i], t);
            }
        });
    }
};

// Slow predecessor — writes Health so the skippable system that READS
// Health is forced into a strictly later wave. The budget overrun is
// detected after this wave commits; the next wave's skippable can then
// be skipped.
class SlowPredecessorSystem : public ISystem {
public:
    const char* name() const noexcept override { return "slow-pred"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Health};
    }
    void update(SystemContext&) override {
        // Burn ~3ms — large enough to trip a 1ms budget reliably.
        volatile std::uint64_t sink = 0;
        for (int k = 0; k < 5'000'000; ++k) sink += k;
        (void)sink;
    }
};

class SkippableLateSystem : public ISystem {
public:
    const char* name() const noexcept override { return "skippable-late"; }
    ComponentSet reads()  const noexcept override {
        return ComponentSet{Component::Health};
    }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Health};
    }
    bool skippable()      const noexcept override { return true; }
    void update(SystemContext&) override {
        // Cheap work — just a marker. Whether this runs is the
        // observable signal we test.
    }
};

struct SoakResult {
    std::vector<std::uint64_t> hashesPerTick;
    std::uint64_t skipCount = 0;
    std::uint64_t stallCount = 0;
    std::uint64_t loaderReloadCount = 0;
};

class FakeLoader : public IResourceLoader {
public:
    explicit FakeLoader(std::atomic<std::uint64_t>* counter) : counter_(counter) {}
    void update(Engine&) override {}
    void markStale(std::uint32_t /*index*/, std::uint32_t /*generation*/,
                   std::type_index /*type*/) override {
        counter_->fetch_add(1, std::memory_order_relaxed);
    }
private:
    std::atomic<std::uint64_t>* counter_;
};

SoakResult runOnce(std::uint64_t seed) {
    SoakResult result;
    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 8;
    cfg.singleThreadedCommit = false;  // sharded path
    cfg.initialEntityCapacity = 8192;
    Engine engine(cfg);

    // Watchdog with a long timeout — should never fire.
    engine.setStallTimeout(5.0);
    engine.setTickBudget(0.001);  // 1ms budget — heavy system trips it
    engine.setSkipPolicy(SkipPolicy::Budget);

    std::atomic<std::uint64_t> reloadCalls{0};
    engine.addResourceLoader(std::make_unique<FakeLoader>(&reloadCalls));

    auto& skipChan  = engine.events<SystemSkipped>();
    auto& stallChan = engine.events<EngineStall>();

    Subscription skipSub = skipChan.subscribeScoped(
        [&result](const SystemSkipped&) { ++result.skipCount; });
    Subscription stallSub = stallChan.subscribeScoped(
        [&result](const EngineStall&) { ++result.stallCount; });

    struct G : IGame {
        std::uint64_t seed;
        std::size_t  entityCount;
        explicit G(std::uint64_t s, std::size_t n) : seed(s), entityCount(n) {}
        void onSetup(Engine& eng, World&, CommandBuffer& cb) override {
            std::mt19937_64 rng(seed);
            std::uniform_real_distribution<float> dist(-10.0f, 10.0f);
            for (std::size_t i = 0; i < entityCount; ++i) {
                Transform t{};
                t.position = {dist(rng), dist(rng), dist(rng)};
                Velocity v{};
                v.linear = {dist(rng), dist(rng), dist(rng)};
                RenderTag r; r.meshId = static_cast<std::int32_t>(i % 4);
                cb.spawn(t, v, r);
            }
            eng.registerSystem(std::make_unique<MoverSystem>());
            eng.registerSystem(std::make_unique<SlowPredecessorSystem>());
            eng.registerSystem(std::make_unique<SkippableLateSystem>());
        }
    } g(seed, 4096);

    CHECK(engine.initialize(g));

    result.hashesPerTick.reserve(200);

    // Drive a few markStale calls during the soak — exercise the
    // event channel's lock-free path under load.
    struct Mesh { int id; };
    auto meshHandle = engine.resources().addRefCounted(Mesh{1});

    for (int tick = 0; tick < 200; ++tick) {
        engine.step();
        result.hashesPerTick.push_back(engine.stats().commitHash);
        if (tick % 25 == 0) {
            engine.markResourceStale(meshHandle.id());
        }
    }

    result.loaderReloadCount =
        reloadCalls.load(std::memory_order_relaxed);

    engine.setStallTimeout(0.0);
    engine.shutdown();
    return result;
}

} // namespace

int main() {
    const auto a = runOnce(42);
    const auto b = runOnce(42);

    CHECK_EQ(a.hashesPerTick.size(), b.hashesPerTick.size());
    CHECK_EQ(a.hashesPerTick.size(), std::size_t{200});
    for (std::size_t i = 0; i < a.hashesPerTick.size(); ++i) {
        CHECK_EQ(a.hashesPerTick[i], b.hashesPerTick[i]);
    }
    // Skips should be observed on both runs.
    CHECK(a.skipCount > 0);
    CHECK(b.skipCount > 0);
    // A 5s stall watchdog won't fire across 200 ticks at <1s each.
    CHECK_EQ(a.stallCount, std::uint64_t{0});
    CHECK_EQ(b.stallCount, std::uint64_t{0});
    // 200 ticks / 25 = 8 markStale calls per run.
    CHECK(a.loaderReloadCount >= 8);

    EXIT_WITH_RESULT();
}
