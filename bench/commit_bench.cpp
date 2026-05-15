// §3.6 batch 13c — Commit-phase microbenchmark.
//
// Compares single-threaded reference commit vs. sharded parallel commit
// across multiple workload shapes and entity counts. Prints a table of
// wall-clock times suitable for documenting the break-even point.
//
// NOT a unit test — bench reports are noisy by nature; if you want a
// deterministic correctness check across both paths, see
// `tests/sharded_commit_test.cpp` and `tests/commit_soak_test.cpp`.
//
// Usage:
//   cmake -S . -B build -DTHREADMAXX_BUILD_BENCHMARKS=ON
//   cmake --build build -j
//   ./build/bench/commit_bench

#include <threadmaxx/threadmaxx.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace threadmaxx;

namespace {

class TransformChurn : public ISystem {
public:
    explicit TransformChurn(std::vector<EntityHandle> es)
        : entities_(std::move(es)) {}
    const char* name() const noexcept override { return "tform-churn"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        const auto t = ctx.tick();
        ctx.parallelFor(static_cast<std::uint32_t>(entities_.size()), 256,
            [this, t](Range r, CommandBuffer& cb) {
                for (auto i = r.begin; i < r.end; ++i) {
                    Transform tr{};
                    tr.position.x = static_cast<float>((i + t) & 0xFF);
                    tr.position.y = static_cast<float>((i * 2 + t) & 0xFF);
                    cb.setTransform(entities_[i], tr);
                }
            });
    }
private:
    std::vector<EntityHandle> entities_;
};

class MixedChurn : public ISystem {
public:
    explicit MixedChurn(std::vector<EntityHandle> es)
        : entities_(std::move(es)) {}
    const char* name() const noexcept override { return "mixed-churn"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override { return ComponentSet::all(); }
    void update(SystemContext& ctx) override {
        const auto t = ctx.tick();
        ctx.parallelFor(static_cast<std::uint32_t>(entities_.size()), 256,
            [this, t](Range r, CommandBuffer& cb) {
                for (auto i = r.begin; i < r.end; ++i) {
                    Transform tr{};
                    tr.position.x = static_cast<float>((i + t) & 0xFF);
                    cb.setTransform(entities_[i], tr);
                    if (((i + t) & 0x3F) == 0) {
                        if ((t & 1) == 0) cb.addTag(entities_[i], Component::StaticTag);
                        else              cb.removeTag(entities_[i], Component::StaticTag);
                    }
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
            Transform tr{};
            tr.position.x = static_cast<float>(i);
            cb.spawn(tr);
        }
    }
};

template <typename SystemFactory>
double runScenario(std::size_t entityCount, int ticks, int workers,
                   bool sharded, SystemFactory factory) {
    Config cfg;
    cfg.sleepToPace          = false;
    cfg.workerCount          = static_cast<std::uint32_t>(workers);
    cfg.deterministic        = true;
    cfg.singleThreadedCommit = !sharded;
    Engine engine(cfg);

    SeedGame game;
    game.count = entityCount;
    engine.initialize(game);

    std::vector<EntityHandle> seeded;
    {
        auto span = engine.world().entities();
        seeded.assign(span.begin(), span.end());
    }
    engine.registerSystem(factory(seeded));

    // Warmup
    for (int i = 0; i < 32; ++i) engine.step();

    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < ticks; ++i) engine.step();
    const auto end = std::chrono::steady_clock::now();

    engine.shutdown();
    return std::chrono::duration<double, std::milli>(end - start).count() /
           static_cast<double>(ticks);
}

void runWorkload(const char* label,
                 std::size_t entityCount,
                 int ticks,
                 int workers,
                 auto factory) {
    const double refMs = runScenario(entityCount, ticks, workers, /*sharded=*/false, factory);
    const double shdMs = runScenario(entityCount, ticks, workers, /*sharded=*/true,  factory);
    const double speedup = shdMs > 0.0 ? refMs / shdMs : 0.0;
    std::printf("  %-26s entities=%7zu  ref=%8.3f ms  shd=%8.3f ms  speedup=%4.2fx\n",
        label, entityCount, refMs, shdMs, speedup);
}

} // namespace

int main() {
    std::printf("threadmaxx commit-phase benchmark (workers=4)\n");
    std::printf("ref = singleThreadedCommit=true (reference)\n");
    std::printf("shd = singleThreadedCommit=false (sharded)\n");
    std::printf("\n");

    constexpr int kTicks = 200;
    constexpr int kWorkers = 4;

    std::printf("Transform-only churn (100%% value-only commands):\n");
    for (std::size_t n : { 256u, 1024u, 8192u, 32768u, 131072u }) {
        runWorkload("transform-only", n, kTicks, kWorkers,
            [](std::vector<EntityHandle> e) {
                return std::make_unique<TransformChurn>(std::move(e));
            });
    }

    std::printf("\nMixed churn (transform + occasional tag flips):\n");
    for (std::size_t n : { 256u, 1024u, 8192u, 32768u }) {
        runWorkload("mixed-tag-flip", n, kTicks, kWorkers,
            [](std::vector<EntityHandle> e) {
                return std::make_unique<MixedChurn>(std::move(e));
            });
    }

    return 0;
}
