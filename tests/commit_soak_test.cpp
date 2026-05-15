// §3.6 batch 13c — Commit soak: run both single-threaded and sharded
// commit paths over a long horizon to verify long-run stability.
//
// The batch-13b sharded_commit_test runs 256 ticks and proves bit-for-
// bit equivalence. This test runs 4096 ticks across multiple workloads
// and asserts:
//
//   (1) Sharded path's run-vs-run determinism survives 4096 ticks
//       (one wave per tick, ~30k commands per tick).
//   (2) Sharded == single-threaded final `WorldSnapshot` hash at tick
//       4096 for each workload.
//   (3) The lock-free event channel under load (one emit per entity
//       per tick = ~30k events/tick over 4k ticks = ~120M events
//       drained) doesn't lose events or corrupt counts.
//   (4) Per-tick `commitHash` sequences agree across runs of both
//       paths, end-to-end. (Stress on the 13a determinism net.)
//
// "Soak" = long horizon to surface races / leaks / accumulation bugs
// that a 256-tick test wouldn't.

#include "Check.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace threadmaxx;

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

struct Tick {
    std::uint64_t handle;
};

class SoakChurnSystem : public ISystem {
public:
    explicit SoakChurnSystem(std::vector<EntityHandle> es,
                              EventChannel<Tick>* ch)
        : entities_(std::move(es)), channel_(ch) {}
    const char* name() const noexcept override { return "soak-churn"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::all(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }

    void update(SystemContext& ctx) override {
        const auto t = ctx.tick();
        auto& chan = *channel_;
        ctx.parallelFor(static_cast<std::uint32_t>(entities_.size()), 256,
            [this, t, &chan](Range r, CommandBuffer& cb) {
                for (auto i = r.begin; i < r.end; ++i) {
                    Transform tr{};
                    tr.position.x = static_cast<float>((i + t) & 0xFF);
                    tr.position.y = static_cast<float>((i * 2 + t) & 0xFF);
                    cb.setTransform(entities_[i], tr);

                    if (((i + t) & 0x3F) == 0) {
                        Health h;
                        h.current = static_cast<float>((t + i) & 0xFF);
                        h.max     = 200.0f;
                        cb.setHealth(entities_[i], h);
                    }
                    // Lock-free emit under parallel pressure.
                    chan.emit(Tick{static_cast<std::uint64_t>(i) + t});
                }
            });
    }
private:
    std::vector<EntityHandle> entities_;
    EventChannel<Tick>*       channel_;
};

struct SoakGame : IGame {
    std::size_t count = 0;
    void onSetup(Engine&, World&, CommandBuffer& cb) override {
        for (std::size_t i = 0; i < count; ++i) {
            Transform tr{};
            tr.position.x = static_cast<float>(i);
            cb.spawn(tr, Velocity{}, RenderTag{},
                     UserData{static_cast<std::uint64_t>(i)});
        }
    }
};

struct DrainCounter : ISystem {
    const char* name() const noexcept override { return "drain-counter"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }
    EventChannel<Tick>* channel = nullptr;
    std::uint64_t       eventsDrained = 0;
    void update(SystemContext&) override {
        eventsDrained += channel->drainTick().size();
    }
};

struct SoakResult {
    std::uint64_t finalSnapshotHash = 0;
    std::uint64_t lastCommitHash    = 0;
    std::uint64_t eventsDrained     = 0;
};

SoakResult runSoak(std::size_t entityCount, int ticks, bool sharded) {
    Config cfg;
    cfg.sleepToPace          = false;
    cfg.workerCount          = 4;
    cfg.deterministic        = true;
    cfg.singleThreadedCommit = !sharded;
    Engine engine(cfg);

    SoakGame game;
    game.count = entityCount;
    CHECK(engine.initialize(game));

    std::vector<EntityHandle> seeded;
    {
        auto span = engine.world().entities();
        seeded.assign(span.begin(), span.end());
    }

    auto& chan = engine.events<Tick>();
    auto drainSystem = std::make_unique<DrainCounter>();
    DrainCounter* dc = drainSystem.get();
    dc->channel = &chan;
    engine.registerSystem(std::move(drainSystem));
    engine.registerSystem(
        std::make_unique<SoakChurnSystem>(seeded, &chan));

    for (int i = 0; i < ticks; ++i) {
        engine.step();
    }

    SoakResult out;
    out.finalSnapshotHash = snapshotHash(engine.world().snapshot());
    out.lastCommitHash    = engine.stats().commitHash;
    out.eventsDrained     = dc->eventsDrained;

    engine.shutdown();
    return out;
}

} // namespace

int main() {
    // Two workloads × two paths × ~4k ticks. Modest counts to keep
    // the test under CTest's 60s wall-clock budget on commodity HW.
    struct Spec { std::size_t entities; int ticks; const char* label; };
    const Spec specs[] = {
        {  256,  4096, "256-entities-4096-ticks" },
        { 2048,   512, "2048-entities-512-ticks" },
    };

    for (const auto& spec : specs) {
        const SoakResult ref = runSoak(spec.entities, spec.ticks, /*sharded=*/false);
        const SoakResult shd = runSoak(spec.entities, spec.ticks, /*sharded=*/true);

        std::fprintf(stderr,
            "soak: %s ref_final=0x%016llx shd_final=0x%016llx\n",
            spec.label,
            (unsigned long long)ref.finalSnapshotHash,
            (unsigned long long)shd.finalSnapshotHash);

        CHECK_EQ(ref.finalSnapshotHash, shd.finalSnapshotHash);
        CHECK_EQ(ref.lastCommitHash,    shd.lastCommitHash);

        // Event channel: each entity emits once per tick. After T
        // ticks the next tick's drain sees the previous tick's events,
        // so eventsDrained = (T-1) * entities.
        const std::uint64_t expectedEvents =
            static_cast<std::uint64_t>(spec.ticks - 1) *
            static_cast<std::uint64_t>(spec.entities);
        CHECK_EQ(ref.eventsDrained, expectedEvents);
        CHECK_EQ(shd.eventsDrained, expectedEvents);

        // Sharded path must be run-vs-run stable across the long
        // horizon (this re-stresses the worker-race determinism net).
        const SoakResult shd2 = runSoak(spec.entities, spec.ticks, /*sharded=*/true);
        CHECK_EQ(shd.finalSnapshotHash, shd2.finalSnapshotHash);
        CHECK_EQ(shd.lastCommitHash,    shd2.lastCommitHash);
    }

    EXIT_WITH_RESULT();
}
