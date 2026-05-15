// §3.6.5 batch 15b — ResourceRegistry refcount churn benchmark.
//
// Measures the cost of `addRefCounted` / handle copy / handle drop
// under concurrent contention. Streaming asset loaders bump and
// release refcounts as game code asks for / releases meshes; the
// per-slot mutex is the bottleneck if many threads churn the same
// slot.
//
// Scenarios:
//   - 1, 2, 4, 8 threads
//   - 100k ops per thread (copy + release sequence)

#include <threadmaxx/threadmaxx.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace threadmaxx;

namespace {

struct MeshPayload {
    int vertexCount = 0;
    float scale = 1.0f;
};

void runScenario(int threads, int opsPerThread) {
    ResourceRegistry reg;
    auto seed = reg.addRefCounted(MeshPayload{1024, 2.5f});

    std::atomic<int> ready{0};
    std::atomic<bool> go{false};

    std::vector<std::thread> ws;
    ws.reserve(threads);
    const auto worker = [&]() {
        ready.fetch_add(1);
        while (!go.load(std::memory_order_acquire)) {}
        for (int i = 0; i < opsPerThread; ++i) {
            // Copy bumps refcount; drop on next line releases.
            auto h = seed;
            (void)h.get();
        }
    };

    for (int t = 0; t < threads; ++t) ws.emplace_back(worker);
    while (ready.load() < threads) {}

    const auto t0 = std::chrono::steady_clock::now();
    go.store(true, std::memory_order_release);
    for (auto& th : ws) th.join();
    const auto t1 = std::chrono::steady_clock::now();

    const double total = std::chrono::duration<double>(t1 - t0).count();
    const long long totalOps = static_cast<long long>(threads) * opsPerThread;
    const double perOp = total * 1e9 / static_cast<double>(totalOps);
    const double mops = static_cast<double>(totalOps) / total / 1e6;
    std::printf("  threads=%d ops=%9lld   %7.1f ns/op   %6.2f Mops/s\n",
                threads, totalOps, perOp, mops);
}

} // namespace

int main() {
    std::printf("=== ResourceRegistry refcount churn ===\n\n");
    for (int t : {1, 2, 4, 8}) {
        runScenario(t, 100'000);
    }
    return 0;
}
