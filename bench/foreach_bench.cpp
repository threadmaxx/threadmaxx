// §3.6.5 batch 15b — Query iteration variants benchmark.
//
// Compares `forEachWith`, `forEachWithCached`, and `forEachChunk`
// across a workload where every entity has Transform + Velocity but
// only half have RenderTag (so the required mask filters out 50%).
//
// Reports per-step wall-clock. Useful for sizing game-code hot loops
// in batch 9's example.

#include <threadmaxx/threadmaxx.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>

using namespace threadmaxx;

namespace {

class ForEachWithSystem : public ISystem {
public:
    const char* name() const noexcept override { return "forEachWith"; }
    ComponentSet reads()  const noexcept override {
        return ComponentSet{Component::Transform} |
               ComponentSet{Component::Velocity};
    }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Transform};
    }
    void update(SystemContext& ctx) override {
        forEachWith<Transform, Velocity>(ctx,
            [](EntityHandle e, const Transform& t, const Velocity& v,
               CommandBuffer& cb) {
                Transform n = t;
                n.position.x += v.linear.x * 0.016f;
                cb.setTransform(e, n);
            });
    }
};

class ForEachWithCachedSystem : public ISystem {
public:
    const char* name() const noexcept override { return "forEachWithCached"; }
    ComponentSet reads()  const noexcept override {
        return ComponentSet{Component::Transform} |
               ComponentSet{Component::Velocity};
    }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Transform};
    }
    void preStep(SystemContext& ctx) override {
        cache_.rebuild(ctx.world(),
                       required<Transform, Velocity>());
    }
    void update(SystemContext& ctx) override {
        forEachWithCached<Transform, Velocity>(ctx, cache_,
            [](EntityHandle e, const Transform& t, const Velocity& v,
               CommandBuffer& cb) {
                Transform n = t;
                n.position.x += v.linear.x * 0.016f;
                cb.setTransform(e, n);
            });
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
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Transform};
    }
    void update(SystemContext& ctx) override {
        forEachChunk<Transform, Velocity>(ctx,
            [](std::span<const EntityHandle> es,
               std::span<const Transform> ts,
               std::span<const Velocity> vs,
               CommandBuffer& cb) {
                for (std::size_t i = 0; i < es.size(); ++i) {
                    Transform n = ts[i];
                    n.position.x += vs[i].linear.x * 0.016f;
                    cb.setTransform(es[i], n);
                }
            });
    }
};

template <typename Sys>
void runScenario(const char* label, std::size_t entityCount) {
    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 4;
    cfg.initialEntityCapacity = static_cast<std::uint32_t>(entityCount + 16);
    Engine engine(cfg);

    struct G : IGame {
        std::size_t entityCount;
        explicit G(std::size_t n) : entityCount(n) {}
        void onSetup(Engine& eng, World&, CommandBuffer& cb) override {
            for (std::size_t i = 0; i < entityCount; ++i) {
                Transform t{};
                Velocity v{};
                v.linear.x = 1.0f;
                if (i % 2 == 0) {
                    RenderTag r; r.meshId = 1;
                    cb.spawn(t, v, r);
                } else {
                    cb.spawn(t, v);
                }
            }
            eng.registerSystem(std::make_unique<Sys>());
        }
    } g(entityCount);
    if (!engine.initialize(g)) { std::printf("  init failed\n"); return; }

    constexpr int kWarmup = 8;
    constexpr int kMeasure = 64;
    for (int i = 0; i < kWarmup; ++i) engine.step();

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kMeasure; ++i) engine.step();
    const auto t1 = std::chrono::steady_clock::now();

    const double total = std::chrono::duration<double>(t1 - t0).count();
    const double perStep = total / kMeasure * 1e6;
    std::printf("  %-20s  N=%6zu   %8.2f us/step\n",
                label, entityCount, perStep);
    engine.shutdown();
}

} // namespace

int main() {
    std::printf("=== Query iteration variants ===\n");
    std::printf("(workers=4, warmup=8, measure=64)\n\n");

    for (std::size_t n : {std::size_t{1'000}, std::size_t{10'000},
                          std::size_t{100'000}}) {
        runScenario<ForEachWithSystem>      ("forEachWith",       n);
        runScenario<ForEachWithCachedSystem>("forEachWithCached", n);
        runScenario<ForEachChunkSystem>     ("forEachChunk",      n);
        std::printf("\n");
    }
    return 0;
}
