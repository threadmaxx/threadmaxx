// §3.6.5 batch 15b — Hierarchy resolution benchmark.
//
// Measures the cost of `HierarchySystem` across:
//   - N = 1k, 10k, 100k entities
//   - chain depth: flat (every entity attached to a root), deep
//     (chains of length 32)
//
// Reports wall-clock per `step()`. Useful before batch 9's
// integration with skeleton-animated meshes (which will stack on
// top of the same hierarchy plumbing).

#include <threadmaxx/threadmaxx.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace threadmaxx;

namespace {

void runScenario(const char* label,
                 std::size_t entityCount,
                 std::size_t chainDepth) {
    Config cfg;
    cfg.sleepToPace = false;
    cfg.workerCount = 4;
    cfg.initialEntityCapacity = static_cast<std::uint32_t>(entityCount + 16);
    Engine engine(cfg);

    struct G : IGame {
        std::size_t entityCount;
        std::size_t chainDepth;
        G(std::size_t n, std::size_t d) : entityCount(n), chainDepth(d) {}
        void onSetup(Engine& eng, World&, CommandBuffer& cb) override {
            // Pre-reserve all handles so we can wire Parent fields.
            std::vector<EntityHandle> handles(entityCount);
            for (auto& h : handles) h = eng.reserveEntityHandle();
            for (std::size_t i = 0; i < entityCount; ++i) {
                Transform t{};
                t.position.x = static_cast<float>(i & 0xFF);
                Parent p{};
                if (chainDepth > 1 && (i % chainDepth) != 0) {
                    p.parent = handles[i - 1];
                }
                cb.spawn(handles[i], t, Velocity{}, RenderTag{},
                         UserData{}, Acceleration{}, p);
            }
            eng.registerSystem(makeHierarchySystem());
        }
    } g(entityCount, chainDepth);
    if (!engine.initialize(g)) { std::printf("  init failed\n"); return; }

    constexpr int kWarmup = 8;
    constexpr int kMeasure = 64;
    for (int i = 0; i < kWarmup; ++i) engine.step();

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kMeasure; ++i) engine.step();
    const auto t1 = std::chrono::steady_clock::now();

    const double total = std::chrono::duration<double>(t1 - t0).count();
    const double perStep = total / kMeasure * 1e6;
    std::printf("  %-22s  N=%6zu  depth=%3zu   %7.2f us/step\n",
                label, entityCount, chainDepth, perStep);

    engine.shutdown();
}

} // namespace

int main() {
    std::printf("=== Hierarchy resolution benchmark ===\n");
    std::printf("(workers=4, warmup=8, measure=64, sharded=false)\n\n");

    runScenario("flat",     1'000,   1);
    runScenario("flat",    10'000,   1);
    runScenario("flat",   100'000,   1);
    runScenario("chain-8",   1'000,  8);
    runScenario("chain-8",  10'000,  8);
    runScenario("chain-8", 100'000,  8);
    runScenario("chain-32",  1'000, 32);
    runScenario("chain-32", 10'000, 32);
    runScenario("chain-32", 100'000, 32);

    return 0;
}
