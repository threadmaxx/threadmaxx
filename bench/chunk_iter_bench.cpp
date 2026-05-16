// §3.9.1 batch 16 — Chunk-iteration comparison benchmark.
//
// Compares four ways of walking the (Transform, Velocity) intersection
// of a workload, on the AI-only and Render+AI canonical scenes:
//
//   - forEachWith<T...>         — per-entity, mask test in the hot path
//   - forEachWithCached<T...>   — MaskCache rebuild in preStep, then a
//                                 dense-index walk (no mask test in the
//                                 inner loop)
//   - forEachChunk<T...>        — chunk-span callback, lowest visible
//                                 indirection
//   - raw masked walk           — manual `world.transforms()` +
//                                 `world.velocities()` + per-entity
//                                 mask check. Not the public path the
//                                 engine recommends; included as the
//                                 "no scheduler / no callback" lower
//                                 bound.
//
// Each system performs a small read-only accumulation (sum of x
// components) into a `volatile` sink to prevent dead-code elimination,
// then publishes the per-step wall-clock to the harness. Reports
// ns/entity and full percentile breakdown.
//
// This bench is the gate for §3.9.2 batch 17 (chunk iteration
// micro-optimization): each candidate landing must show a measured win
// against this baseline.

#include "common.hpp"
#include "scene_workloads.hpp"

#include <threadmaxx/Query.hpp>
#include <threadmaxx/threadmaxx.hpp>

#include <cstdio>
#include <memory>
#include <string>

namespace {

using namespace threadmaxx;
using namespace threadmaxx_bench;

volatile double g_sink = 0.0;

class ForEachWithSystem : public ISystem {
public:
    const char* name() const noexcept override { return "forEachWith"; }
    ComponentSet reads()  const noexcept override {
        return ComponentSet{Component::Transform} |
               ComponentSet{Component::Velocity};
    }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }
    void update(SystemContext& ctx) override {
        double sum = 0.0;
        forEachWith<Transform, Velocity>(ctx,
            [&sum](EntityHandle, const Transform& t, const Velocity& v,
                   CommandBuffer&) {
                sum += t.position.x + v.linear.x;
            });
        g_sink = sum;
    }
};

class ForEachWithCachedSystem : public ISystem {
public:
    const char* name() const noexcept override { return "forEachWithCached"; }
    ComponentSet reads()  const noexcept override {
        return ComponentSet{Component::Transform} |
               ComponentSet{Component::Velocity};
    }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }
    void preStep(SystemContext& ctx) override {
        cache_.rebuild(ctx.world(), required<Transform, Velocity>());
    }
    void update(SystemContext& ctx) override {
        double sum = 0.0;
        forEachWithCached<Transform, Velocity>(ctx, cache_,
            [&sum](EntityHandle, const Transform& t, const Velocity& v,
                   CommandBuffer&) {
                sum += t.position.x + v.linear.x;
            });
        g_sink = sum;
    }
private:
    MaskCache cache_;
};

class ForEachChunkSystem : public ISystem {
public:
    const char* name() const noexcept override { return "forEachChunk"; }
    ComponentSet reads()  const noexcept override {
        return ComponentSet{Component::Transform} |
               ComponentSet{Component::Velocity};
    }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }
    void update(SystemContext& ctx) override {
        double sum = 0.0;
        forEachChunk<Transform, Velocity>(ctx,
            [&sum](std::span<const EntityHandle>,
                   std::span<const Transform> ts,
                   std::span<const Velocity> vs,
                   CommandBuffer&) {
                for (std::size_t i = 0; i < ts.size(); ++i)
                    sum += ts[i].position.x + vs[i].linear.x;
            });
        g_sink = sum;
    }
};

class RawMaskedWalkSystem : public ISystem {
public:
    const char* name() const noexcept override { return "rawMaskedWalk"; }
    ComponentSet reads()  const noexcept override {
        return ComponentSet{Component::Transform} |
               ComponentSet{Component::Velocity};
    }
    ComponentSet writes() const noexcept override { return ComponentSet::none(); }
    void update(SystemContext& ctx) override {
        const auto& w = ctx.world();
        const auto ts = w.transforms();
        const auto vs = w.velocities();
        const auto ms = w.componentMasks();
        const ComponentSet req = required<Transform, Velocity>();
        double sum = 0.0;
        const std::size_t n = ms.size();
        for (std::size_t i = 0; i < n; ++i) {
            if (ms[i].hasAll(req)) {
                sum += ts[i].position.x + vs[i].linear.x;
            }
        }
        g_sink = sum;
    }
};

template <typename Sys>
LatencyHistogram measureSystem(IGame& workload, std::uint32_t entityCount,
                               int warmup, int iters) {
    Engine engine(benchConfig(/*workers=*/4, entityCount));
    if (!engine.initialize(workload)) {
        std::printf("  init failed\n");
        return {};
    }
    engine.registerSystem(std::make_unique<Sys>());

    LatencyHistogram h;
    runIters(h, warmup, iters, [&] { engine.step(); });
    engine.shutdown();
    return h;
}

void emit(CsvWriter& csv, const std::string& label, const std::string& workload,
          std::size_t entities, const LatencyHistogram& h) {
    BenchRow r;
    r.label    = label;
    r.workload = workload;
    r.entities = entities;
    r.workers  = 4;
    r.mean_ns  = h.meanNs();
    r.stddev   = h.stddev();
    r.p50_ns   = h.p50Ns();
    r.p95_ns   = h.p95Ns();
    r.p99_ns   = h.p99Ns();
    if (h.meanNs() > 0.0 && entities > 0) {
        r.throughput = static_cast<double>(entities) /
                       (h.meanNs() / 1e9);
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "ns_per_entity=%.2f",
                  h.meanNs() / static_cast<double>(entities));
    r.note = buf;
    csv.row(r);
}

} // namespace

int main(int argc, char** argv) {
    const char* csvPath = (argc >= 2) ? argv[1] : nullptr;
    CsvWriter csv(csvPath);

    constexpr int kWarmup = 8;
    constexpr int kIters  = 64;

    {
        std::printf("# AI-only workload (N=%u)\n", kAiCount);
        AiOnlyWorkload w;
        const auto a = measureSystem<ForEachWithSystem>      (w, kAiCount, kWarmup, kIters);
        emit(csv, "forEachWith", "AI", kAiCount, a);
        const auto b = measureSystem<ForEachWithCachedSystem>(w, kAiCount, kWarmup, kIters);
        emit(csv, "forEachWithCached", "AI", kAiCount, b);
        const auto c = measureSystem<ForEachChunkSystem>     (w, kAiCount, kWarmup, kIters);
        emit(csv, "forEachChunk", "AI", kAiCount, c);
        const auto d = measureSystem<RawMaskedWalkSystem>    (w, kAiCount, kWarmup, kIters);
        emit(csv, "rawMaskedWalk", "AI", kAiCount, d);
    }
    {
        std::printf("# Render+AI workload (N=%u)\n", kRenderCount);
        RenderAiWorkload w;
        const auto a = measureSystem<ForEachWithSystem>      (w, kRenderCount, kWarmup, kIters);
        emit(csv, "forEachWith", "Render+AI", kRenderCount, a);
        const auto b = measureSystem<ForEachWithCachedSystem>(w, kRenderCount, kWarmup, kIters);
        emit(csv, "forEachWithCached", "Render+AI", kRenderCount, b);
        const auto c = measureSystem<ForEachChunkSystem>     (w, kRenderCount, kWarmup, kIters);
        emit(csv, "forEachChunk", "Render+AI", kRenderCount, c);
        const auto d = measureSystem<RawMaskedWalkSystem>    (w, kRenderCount, kWarmup, kIters);
        emit(csv, "rawMaskedWalk", "Render+AI", kRenderCount, d);
    }

    std::printf("# g_sink=%g (kept alive to defeat DCE)\n", g_sink);
    return 0;
}
