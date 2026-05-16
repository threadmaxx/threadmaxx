// §3.11.7 batch D7 — simulated boot-time asset loader.
//
// Implements `IResourceLoader` with a tick-based fake "I/O queue":
// the constructor seeds `pendingLoads = kAssetCount` (default 64);
// each `update()` migrates ~4 items per tick from pending → ready,
// simulating an asynchronous loader without actually touching disk.
// The point is to exercise:
//   - `Engine::preloadUntil(done, timeout)` at boot, blocking the
//     splash screen until the queue drains.
//   - `LoaderStats` aggregation via `Engine::aggregateLoaderStats()`,
//     surfaced in the HUD.
//   - `IResourceLoader::onShutdown` reverse-order teardown contract.
//
// Real .obj / texture / shader file I/O is renderer-side work
// deferred to a future `batch 9b` (alongside skinning). The procedural
// path here demonstrates the engine surface against a fake workload.

#pragma once

#include <threadmaxx/Resource.hpp>

#include <cstdint>

namespace rpg {

class PreloadLoader : public threadmaxx::IResourceLoader {
public:
    static constexpr std::uint64_t kAssetCount = 64;
    /// Items moved from pending → ready per `update()` call. Sized so
    /// the loader takes ~16 ticks to drain, i.e. ~0.27 s at 60 Hz.
    static constexpr std::uint64_t kPerTick    = 4;

    PreloadLoader();

    void update(threadmaxx::Engine& engine) override;
    void onShutdown(threadmaxx::Engine& engine) override;
    threadmaxx::LoaderStats stats() const noexcept override { return stats_; }

    bool allDone() const noexcept { return stats_.pendingLoads == 0; }

private:
    threadmaxx::LoaderStats stats_{};
};

} // namespace rpg
