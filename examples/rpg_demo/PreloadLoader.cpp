#include "PreloadLoader.hpp"

#include <threadmaxx/Engine.hpp>

#include <algorithm>
#include <cstdio>

namespace rpg {

PreloadLoader::PreloadLoader() {
    stats_.pendingLoads = kAssetCount;
    stats_.memoryFootprint = 0;
    stats_.memoryBudget    = 256u * 1024u * 1024u;  // 256 MiB sentinel
}

void PreloadLoader::update(threadmaxx::Engine& engine) {
    (void)engine;
    if (stats_.pendingLoads == 0) return;
    const std::uint64_t drain = std::min(kPerTick, stats_.pendingLoads);
    stats_.pendingLoads -= drain;
    stats_.ready        += drain;
    // Simulate per-asset memory footprint: 64 KiB each.
    stats_.memoryFootprint += drain * 64u * 1024u;
}

void PreloadLoader::onShutdown(threadmaxx::Engine& engine) {
    (void)engine;
    std::printf("[preload] loader shutdown: %llu ready, %llu pending\n",
                static_cast<unsigned long long>(stats_.ready),
                static_cast<unsigned long long>(stats_.pendingLoads));
}

} // namespace rpg
