// §3.6.5 batch 15b — Frustum culling throughput benchmark.
//
// Measures `cullByFrustum` across cartesian products of:
//   - draw item counts: 1k, 10k, 100k
//   - camera counts: 1, 4, 8, 16, 32
//
// Pure compute kernel; no engine setup. Used to size the upper
// bound on the Vulkan reference renderer's per-frame visibility
// budget.

#include <threadmaxx/render/Visibility.hpp>
#include <threadmaxx/Components.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

using namespace threadmaxx;

namespace {

void runScenario(std::size_t items, std::size_t cameras) {
    std::vector<DrawItem>       drawItems(items);
    std::vector<BoundingVolume> bounds(items);
    std::vector<Camera>         cams(cameras);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-100.0f, 100.0f);
    for (std::size_t i = 0; i < items; ++i) {
        bounds[i].min = {dist(rng), dist(rng), dist(rng)};
        bounds[i].max = {bounds[i].min.x + 1.0f,
                         bounds[i].min.y + 1.0f,
                         bounds[i].min.z + 1.0f};
    }
    for (std::size_t i = 0; i < cameras; ++i) {
        cams[i].id = static_cast<std::uint32_t>(i);
    }

    constexpr int kIter = 256;
    // Warmup.
    for (int i = 0; i < 8; ++i) {
        cullByFrustum(drawItems, bounds, cams);
    }

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIter; ++i) {
        cullByFrustum(drawItems, bounds, cams);
    }
    const auto t1 = std::chrono::steady_clock::now();

    const double total = std::chrono::duration<double>(t1 - t0).count();
    const double perCall = total / kIter * 1e6;
    const double itemsPerSec = static_cast<double>(items) * kIter / total / 1e6;
    std::printf("  items=%6zu cams=%2zu   %8.2f us/call   %7.1f Mitems/s\n",
                items, cameras, perCall, itemsPerSec);
}

} // namespace

int main() {
    std::printf("=== cullByFrustum throughput ===\n\n");
    for (std::size_t cams : {std::size_t{1}, std::size_t{4},
                             std::size_t{8}, std::size_t{16},
                             std::size_t{32}}) {
        for (std::size_t items : {std::size_t{1'000},
                                  std::size_t{10'000},
                                  std::size_t{100'000}}) {
            runScenario(items, cams);
        }
        std::printf("\n");
    }
    return 0;
}
