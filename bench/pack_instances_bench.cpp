// §3.6.5 batch 15b — `InstanceBufferLayout::packInstances` throughput.
//
// Measures how fast `packInstances` projects a span of DrawItems into
// the 128-byte std140-friendly layout. Used in the Vulkan reference
// renderer's per-frame upload step.

#include <threadmaxx/render/DrawItem.hpp>
#include <threadmaxx/render/InstanceBufferLayout.hpp>

#include <chrono>
#include <cstdio>
#include <vector>

using namespace threadmaxx;

namespace {

void runScenario(std::size_t n) {
    std::vector<DrawItem> items(n);
    for (std::size_t i = 0; i < n; ++i) {
        items[i].transform.position.x = static_cast<float>(i);
    }
    std::vector<InstanceLayoutEntry> out(n);

    constexpr int kIter = 256;
    // Warmup.
    for (int i = 0; i < 8; ++i) packInstances(items, out);

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIter; ++i) packInstances(items, out);
    const auto t1 = std::chrono::steady_clock::now();

    const double total = std::chrono::duration<double>(t1 - t0).count();
    const double perCall = total / kIter * 1e6;
    const double rate = static_cast<double>(n) * kIter / total / 1e6;
    std::printf("  n=%6zu   %8.2f us/call   %7.1f Mitems/s\n",
                n, perCall, rate);
}

} // namespace

int main() {
    std::printf("=== packInstances throughput ===\n\n");
    for (std::size_t n : {std::size_t{1'000}, std::size_t{10'000},
                          std::size_t{100'000}}) {
        runScenario(n);
    }
    return 0;
}
