// §3.9.1 batch 16 — Commit-path per-variant cost breakdown.
//
// Existing `commit_bench.cpp` (batch 13c) compares single-threaded vs
// sharded commit on broad workloads. This bench complements it by
// breaking out the cost by command variant on the Churn workload, so
// §3.9.3 batch 18 (command buffer arena + compact payloads) has a
// per-variant baseline to clear.
//
// Methodology: register one system per variant. Each tick, the system
// issues N value-only commands of its type against every entity in
// the workload. Records per-tick wall-clock; reports ns/cmd, mean and
// percentiles, for both `singleThreadedCommit={true,false}`.

#include "common.hpp"
#include "scene_workloads.hpp"

#include <threadmaxx/threadmaxx.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace threadmaxx;
using namespace threadmaxx_bench;

class SetTransformChurn : public ISystem {
public:
    explicit SetTransformChurn(std::vector<EntityHandle> es)
        : entities_(std::move(es)) {}
    const char* name() const noexcept override { return "setTransform"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Transform};
    }
    void update(SystemContext& ctx) override {
        const auto t = ctx.tick();
        ctx.parallelFor(static_cast<std::uint32_t>(entities_.size()), 256,
            [this, t](Range r, CommandBuffer& cb) {
                for (auto i = r.begin; i < r.end; ++i) {
                    Transform tr{};
                    tr.position.x = static_cast<float>((i + t) & 0xFF);
                    cb.setTransform(entities_[i], tr);
                }
            });
    }
private:
    std::vector<EntityHandle> entities_;
};

class SetVelocityChurn : public ISystem {
public:
    explicit SetVelocityChurn(std::vector<EntityHandle> es)
        : entities_(std::move(es)) {}
    const char* name() const noexcept override { return "setVelocity"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Velocity};
    }
    void update(SystemContext& ctx) override {
        const auto t = ctx.tick();
        ctx.parallelFor(static_cast<std::uint32_t>(entities_.size()), 256,
            [this, t](Range r, CommandBuffer& cb) {
                for (auto i = r.begin; i < r.end; ++i) {
                    Velocity v{};
                    v.linear.x = static_cast<float>(((i + t) & 0xFF)) * 0.01f;
                    cb.setVelocity(entities_[i], v);
                }
            });
    }
private:
    std::vector<EntityHandle> entities_;
};

class AddTagChurn : public ISystem {
public:
    explicit AddTagChurn(std::vector<EntityHandle> es)
        : entities_(std::move(es)) {}
    const char* name() const noexcept override { return "addRemoveTag"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        const bool addPhase = (ctx.tick() & 1) == 0;
        ctx.parallelFor(static_cast<std::uint32_t>(entities_.size()), 256,
            [this, addPhase](Range r, CommandBuffer& cb) {
                for (auto i = r.begin; i < r.end; ++i) {
                    if (addPhase) cb.addTag(entities_[i], Component::StaticTag);
                    else          cb.removeTag(entities_[i], Component::StaticTag);
                }
            });
    }
private:
    std::vector<EntityHandle> entities_;
};

class SpawnDestroyChurn : public ISystem {
public:
    SpawnDestroyChurn(std::uint32_t spawnPerTick)
        : spawnPerTick_(spawnPerTick) {}
    const char* name() const noexcept override { return "spawnDestroy"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        const bool spawnPhase = (ctx.tick() & 1) == 0;
        if (spawnPhase) {
            ctx.single([this](Range, CommandBuffer& cb) {
                for (std::uint32_t i = 0; i < spawnPerTick_; ++i) {
                    Transform tr{};
                    tr.position.x = static_cast<float>(i);
                    cb.spawn(tr);
                }
            });
        } else {
            // Pull a fresh entity list each tick so we destroy what
            // the previous tick added.
            const auto entSpan = ctx.world().entities();
            const std::size_t toKill =
                std::min<std::size_t>(spawnPerTick_, entSpan.size());
            std::vector<EntityHandle> kill(entSpan.end() - toKill,
                                           entSpan.end());
            ctx.single([kill = std::move(kill)](Range, CommandBuffer& cb) {
                for (auto e : kill) cb.destroy(e);
            });
        }
    }
private:
    std::uint32_t spawnPerTick_;
};

// §3.9.6 batch 21 — multi-archetype seed: spawns 100k entities split
// across many distinct masks so the sharded commit's chunk-bin path
// has real parallelism to exploit. 4 archetype shapes × 25k entities
// each: Transform-only, +Velocity, +Velocity+Health, +Velocity+Health+
// BoundingVolume. The setTransform workload then has commands spread
// evenly across all 4 chunks, so a 4-worker engine can run all four
// bins in parallel.
struct MultiArchWorkload : threadmaxx::IGame {
    std::uint32_t count = kChurnCount;

    void onSetup(Engine&, World&, CommandBuffer& cb) override {
        for (std::uint32_t i = 0; i < count; ++i) {
            Bundle b{};
            b.transform.position.x = static_cast<float>(i);
            b.initialMask = ComponentSet{Component::Transform};
            const std::uint32_t kind = i & 3u;
            if (kind >= 1u) {
                b.velocity.linear.x = 1.0f;
                b.initialMask = b.initialMask | ComponentSet{Component::Velocity};
            }
            if (kind >= 2u) {
                b.health = Health{50.0f, 50.0f};
                b.initialMask = b.initialMask | ComponentSet{Component::Health};
            }
            if (kind >= 3u) {
                b.boundingVolume = BoundingVolume{{-1,-1,-1},{1,1,1}};
                b.initialMask = b.initialMask |
                    ComponentSet{Component::BoundingVolume};
            }
            cb.spawnBundle(b);
        }
    }
};

template <typename Factory>
LatencyHistogram measure(bool sharded, std::uint32_t workers,
                         int warmup, int iters, Factory factory) {
    Config cfg = benchConfig(workers, kChurnCount, sharded);
    Engine engine(cfg);
    ChurnWorkload workload;
    if (!engine.initialize(workload)) {
        std::printf("  init failed\n");
        return {};
    }

    std::vector<EntityHandle> seeded;
    {
        const auto s = engine.world().entities();
        seeded.assign(s.begin(), s.end());
    }
    engine.registerSystem(factory(seeded));

    LatencyHistogram h;
    runIters(h, warmup, iters, [&] { engine.step(); });
    engine.shutdown();
    return h;
}

template <typename Factory>
LatencyHistogram measureMultiArch(bool sharded, std::uint32_t workers,
                                  int warmup, int iters, Factory factory) {
    Config cfg = benchConfig(workers, kChurnCount, sharded);
    Engine engine(cfg);
    MultiArchWorkload workload;
    if (!engine.initialize(workload)) {
        std::printf("  init failed\n");
        return {};
    }
    std::vector<EntityHandle> seeded;
    {
        const auto s = engine.world().entities();
        seeded.assign(s.begin(), s.end());
    }
    engine.registerSystem(factory(seeded));

    LatencyHistogram h;
    runIters(h, warmup, iters, [&] { engine.step(); });
    engine.shutdown();
    return h;
}

void emit(CsvWriter& csv, const std::string& label, bool sharded,
          std::uint32_t workers, std::size_t entityCount,
          const LatencyHistogram& h, std::size_t cmdsPerTick) {
    BenchRow r;
    r.label    = label;
    r.workload = sharded ? "Churn/sharded" : "Churn/single";
    r.entities = entityCount;
    r.workers  = workers;
    r.mean_ns  = h.meanNs();
    r.stddev   = h.stddev();
    r.p50_ns   = h.p50Ns();
    r.p95_ns   = h.p95Ns();
    r.p99_ns   = h.p99Ns();
    if (h.meanNs() > 0.0 && cmdsPerTick > 0) {
        r.throughput =
            static_cast<double>(cmdsPerTick) / (h.meanNs() / 1e9);
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "ns_per_cmd=%.2f",
                  h.meanNs() / static_cast<double>(cmdsPerTick));
    r.note = buf;
    csv.row(r);
}

} // namespace

int main(int argc, char** argv) {
    const char* csvPath = (argc >= 2) ? argv[1] : nullptr;
    CsvWriter csv(csvPath);

    constexpr int kWarmup  = 16;
    constexpr int kIters   = 128;
    constexpr std::uint32_t kWorkers = 4;
    const std::size_t kPerTick = kChurnCount;
    const std::size_t kSpawnPerTick = 1024;

    for (bool sharded : {false, true}) {
        const auto h1 = measure(sharded, kWorkers, kWarmup, kIters,
            [](std::vector<EntityHandle> e) {
                return std::make_unique<SetTransformChurn>(std::move(e));
            });
        emit(csv, "setTransform", sharded, kWorkers, kChurnCount, h1, kPerTick);

        const auto h2 = measure(sharded, kWorkers, kWarmup, kIters,
            [](std::vector<EntityHandle> e) {
                return std::make_unique<SetVelocityChurn>(std::move(e));
            });
        emit(csv, "setVelocity", sharded, kWorkers, kChurnCount, h2, kPerTick);

        const auto h3 = measure(sharded, kWorkers, kWarmup, kIters,
            [](std::vector<EntityHandle> e) {
                return std::make_unique<AddTagChurn>(std::move(e));
            });
        emit(csv, "addRemoveTag", sharded, kWorkers, kChurnCount, h3, kPerTick);

        const auto h4 = measure(sharded, kWorkers, kWarmup, kIters,
            [&](std::vector<EntityHandle>) {
                return std::make_unique<SpawnDestroyChurn>(
                    static_cast<std::uint32_t>(kSpawnPerTick));
            });
        emit(csv, "spawnDestroy", sharded, kWorkers, kChurnCount, h4, kSpawnPerTick);
    }

    // §3.9.6 batch 21 — multi-archetype workload. 4 distinct chunks,
    // 25k entities each. setTransform commands distribute evenly so
    // the sharded path can dispatch 4 parallel jobs.
    for (bool sharded : {false, true}) {
        const auto h = measureMultiArch(sharded, kWorkers, kWarmup, kIters,
            [](std::vector<EntityHandle> e) {
                return std::make_unique<SetTransformChurn>(std::move(e));
            });
        BenchRow r;
        r.label    = "setTransform";
        r.workload = sharded ? "MultiArch/sharded" : "MultiArch/single";
        r.entities = kChurnCount;
        r.workers  = kWorkers;
        r.mean_ns  = h.meanNs();
        r.stddev   = h.stddev();
        r.p50_ns   = h.p50Ns();
        r.p95_ns   = h.p95Ns();
        r.p99_ns   = h.p99Ns();
        if (h.meanNs() > 0.0 && kPerTick > 0) {
            r.throughput =
                static_cast<double>(kPerTick) / (h.meanNs() / 1e9);
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "ns_per_cmd=%.2f",
                      h.meanNs() / static_cast<double>(kPerTick));
        r.note = buf;
        csv.row(r);
    }
    return 0;
}
