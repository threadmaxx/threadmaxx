// §3.6 batch 13c — EventChannel emit-throughput microbench.
//
// Measures the cost of `emit` from N concurrent producer threads with
// the lock-free MPSC channel. There's no reference implementation to
// compare against (the mutex version is gone), so the bench just
// reports throughput at varying contention levels.

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

using namespace threadmaxx;

namespace {

struct Stamp {
    std::uint32_t a;
    std::uint64_t b;
};

void runBench(int producers, std::uint64_t emitsPerProducer) {
    Config cfg; cfg.sleepToPace = false; cfg.workerCount = 1;
    Engine engine(cfg);
    struct G : IGame { void onSetup(Engine&, World&, CommandBuffer&) override {} } g;
    engine.initialize(g);
    auto& chan = engine.events<Stamp>();

    std::vector<std::thread> ts;
    std::atomic<bool> go{false};
    std::atomic<int>  ready{0};
    for (int p = 0; p < producers; ++p) {
        ts.emplace_back([&, p]() {
            ready.fetch_add(1, std::memory_order_release);
            while (!go.load(std::memory_order_acquire)) std::this_thread::yield();
            for (std::uint64_t i = 0; i < emitsPerProducer; ++i) {
                chan.emit(Stamp{static_cast<std::uint32_t>(p), i});
            }
        });
    }
    while (ready.load(std::memory_order_acquire) < producers) std::this_thread::yield();

    const auto t0 = std::chrono::steady_clock::now();
    go.store(true, std::memory_order_release);
    for (auto& t : ts) t.join();
    const auto t1 = std::chrono::steady_clock::now();

    const double seconds = std::chrono::duration<double>(t1 - t0).count();
    const std::uint64_t totalEmits = static_cast<std::uint64_t>(producers) * emitsPerProducer;
    const double mEmitsPerSec = totalEmits / seconds / 1e6;
    std::printf("  producers=%2d total=%9llu emits  %.3f s  %.2f M emits/s  (%.1f ns/emit)\n",
        producers, (unsigned long long)totalEmits,
        seconds, mEmitsPerSec, seconds * 1e9 / totalEmits);

    engine.step();        // drain the back stack
    engine.shutdown();
}

} // namespace

int main() {
    std::printf("threadmaxx EventChannel lock-free emit microbench\n\n");

    constexpr std::uint64_t kEmitsPerProducer = 200000;
    for (int p : { 1, 2, 4, 8, 16 }) {
        runBench(p, kEmitsPerProducer);
    }
    return 0;
}
