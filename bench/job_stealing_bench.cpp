// §3.6.5 batch 15b — Worker steal-ratio sweep.
//
// Drives a `parallelFor` workload at various grain sizes and reports
// the resulting `JobSystemStats::stolenJobs / totalJobs` ratio. A
// healthy ratio is small (worker pool is keeping itself fed without
// excess imbalance recovery); a high ratio means the chunks were too
// large for the worker count or some chunks were structurally heavier
// than others.

#include <threadmaxx/threadmaxx.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>

using namespace threadmaxx;

namespace {

class WorkloadSystem : public ISystem {
public:
    std::uint32_t grain = 256;
    std::uint32_t loadFactor = 1000;
    const char* name() const noexcept override { return "workload"; }
    ComponentSet reads()  const noexcept override { return ComponentSet::none(); }
    ComponentSet writes() const noexcept override {
        return ComponentSet{Component::Transform};
    }
    void update(SystemContext& ctx) override {
        const auto n = static_cast<std::uint32_t>(ctx.world().size());
        ctx.parallelFor(n, grain,
            [load = loadFactor](Range r, CommandBuffer&) {
                volatile std::uint64_t sink = 0;
                for (std::uint32_t i = r.begin; i < r.end; ++i) {
                    for (std::uint32_t k = 0; k < load; ++k) sink += k;
                }
                (void)sink;
            });
    }
};

void runScenario(std::uint32_t workers, std::uint32_t grain,
                 std::size_t entityCount, std::uint32_t load) {
    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = workers;
    cfg.initialEntityCapacity = static_cast<std::uint32_t>(entityCount + 16);
    Engine engine(cfg);

    WorkloadSystem* ptr = nullptr;
    struct G : IGame {
        std::size_t entityCount;
        std::uint32_t grain;
        std::uint32_t load;
        WorkloadSystem** out;
        G(std::size_t n, std::uint32_t g, std::uint32_t l,
          WorkloadSystem** o)
            : entityCount(n), grain(g), load(l), out(o) {}
        void onSetup(Engine& eng, World&, CommandBuffer& cb) override {
            for (std::size_t i = 0; i < entityCount; ++i) {
                cb.spawn(Transform{}, Velocity{});
            }
            auto sys = std::make_unique<WorkloadSystem>();
            sys->grain = grain;
            sys->loadFactor = load;
            *out = sys.get();
            eng.registerSystem(std::move(sys));
        }
    } g(entityCount, grain, load, &ptr);
    if (!engine.initialize(g)) { std::printf("  init failed\n"); return; }

    constexpr int kIter = 64;
    for (int i = 0; i < 4; ++i) engine.step();  // warmup

    const auto preStats = engine.jobSystemStats();

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIter; ++i) engine.step();
    const auto t1 = std::chrono::steady_clock::now();

    const auto postStats = engine.jobSystemStats();
    const double total = std::chrono::duration<double>(t1 - t0).count();
    const double perStep = total / kIter * 1e6;
    const auto deltaJobs = postStats.totalJobs - preStats.totalJobs;
    const auto deltaStolen = postStats.stolenJobs - preStats.stolenJobs;
    const double stealPct = deltaJobs > 0
        ? static_cast<double>(deltaStolen) / static_cast<double>(deltaJobs) * 100.0
        : 0.0;
    std::printf("  w=%2u N=%6zu grain=%5u   %8.2f us/step  "
                "jobs/step=%6.0f  steal=%5.1f%%\n",
                workers, entityCount, grain, perStep,
                static_cast<double>(deltaJobs) / kIter,
                stealPct);
    engine.shutdown();
}

} // namespace

int main() {
    std::printf("=== Worker steal-ratio sweep ===\n");
    std::printf("(load=1000 iterations per item; warmup=4, measure=64)\n\n");

    for (std::uint32_t w : {2u, 4u, 8u}) {
        for (std::size_t n : {std::size_t{1'000}, std::size_t{10'000},
                              std::size_t{100'000}}) {
            for (std::uint32_t g : {64u, 256u, 1024u}) {
                runScenario(w, g, n, 1000);
            }
        }
        std::printf("\n");
    }
    return 0;
}
