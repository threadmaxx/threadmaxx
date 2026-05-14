#pragma once

#include "JobSystem.hpp"
#include "WorldImpl.hpp"

#include "threadmaxx/CommandBuffer.hpp"
#include "threadmaxx/Config.hpp"
#include "threadmaxx/Logger.hpp"
#include "threadmaxx/RenderFrame.hpp"
#include "threadmaxx/Resource.hpp"
#include "threadmaxx/ScratchArena.hpp"
#include "threadmaxx/Stats.hpp"
#include "threadmaxx/System.hpp"
#include "threadmaxx/World.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace threadmaxx {
class Engine;
class IRenderer;
class IGame;
}

namespace threadmaxx::internal {

// Concrete SystemContext handed to ISystem::update(). It captures everything
// a system needs to schedule parallel work and read the world.
class SystemContextImpl : public SystemContext {
public:
    SystemContextImpl(class EngineImpl& engine, const World& world,
                      double dt, std::uint64_t tick)
        : engine_(engine), world_(world), dt_(dt), tick_(tick) {}

    const World& world() const noexcept override { return world_; }
    double       dt()    const noexcept override { return dt_; }
    std::uint64_t tick() const noexcept override { return tick_; }

    void parallelFor(std::uint32_t count, std::uint32_t grain, JobFn fn) override;
    void parallelFor(std::uint32_t count, std::uint32_t grain, JobFnArena fn) override;
    void single(JobFn fn) override;
    void single(JobFnArena fn) override;
    EntityHandle reserveHandle() override;
    std::uint32_t reserveHandles(std::uint32_t count,
                                 std::span<EntityHandle> out) override;

    // Per-system list of command buffers, in submission order. The
    // matching ScratchArena (if any) lives in `arenas_[i]` — they grow
    // in lockstep; arenas_[i] is default-constructed (empty) for chunks
    // submitted without arena support.
    std::vector<CommandBuffer>&  buffers() noexcept { return buffers_; }
    std::vector<ScratchArena>&   arenas()  noexcept { return arenas_; }

    // Number of jobs handed to JobSystem from this context. parallelFor
    // chunks count; single() does not (it runs inline).
    std::uint64_t jobsSubmitted() const noexcept { return jobsSubmitted_; }

    // §3.1 SystemStats extras. waitSeconds: cumulative time spent in
    // parallelFor's latch wait — i.e. how long the system's own thread
    // sat blocked waiting for workers. peakQueueDepth: max value of
    // JobSystem::outstanding() sampled right after each parallelFor
    // submit, recording wave congestion the system saw.
    double        waitSeconds()    const noexcept { return waitSeconds_; }
    std::uint32_t peakQueueDepth() const noexcept { return peakQueueDepth_; }

private:
    class EngineImpl& engine_;
    const World&      world_;
    double            dt_;
    std::uint64_t     tick_;
    std::vector<CommandBuffer> buffers_;
    std::vector<ScratchArena>  arenas_;   // parallel to buffers_; same length
    std::uint64_t     jobsSubmitted_ = 0;
    double            waitSeconds_   = 0.0;
    std::uint32_t     peakQueueDepth_ = 0;
};

// The full engine state. Hidden behind PImpl from the public Engine class.
class EngineImpl {
public:
    explicit EngineImpl(const Config& cfg);
    ~EngineImpl();

    bool initialize(IGame& game, Engine& publicEngine);
    void step();
    void run();
    void shutdown();

    void requestQuit() noexcept { quit_.store(true, std::memory_order_release); }
    bool quitRequested() const noexcept { return quit_.load(std::memory_order_acquire); }

    void registerSystem(std::unique_ptr<ISystem> system);
    std::size_t registerSystemAt(std::size_t position,
                                 std::unique_ptr<ISystem> system);
    std::size_t registeredSystemCount() const noexcept { return systems_.size(); }
    void setRenderer(IRenderer* renderer) noexcept { renderer_ = renderer; }

    void setLogger(ILogger* logger) noexcept { logger_ = logger; }
    ILogger& logger() noexcept { return logger_ ? *logger_ : defaultLogger_; }

    World&        world()        noexcept { return world_; }
    const World&  world() const  noexcept { return world_; }

    // §3.3 resource loaders. The engine pumps update() once per tick
    // after the postStep hooks commit. addLoader returns a non-owning
    // pointer for inspection / tests.
    IResourceLoader* addResourceLoader(std::unique_ptr<IResourceLoader> loader);

    std::size_t resourceLoaderCount() const noexcept {
        return resourceLoaders_.size();
    }

    LoaderStats aggregateLoaderStats() const noexcept;

    void markResourceStale(std::uint32_t index,
                           std::uint32_t generation,
                           std::type_index type);

    bool preloadUntil(std::function<bool()> done,
                      std::chrono::milliseconds timeout);
    const Config& config() const noexcept { return cfg_; }
    std::uint64_t tick()   const noexcept { return tick_; }
    double simulationTime() const noexcept { return simulationTime_; }

    EngineStats stats() const noexcept { return stats_; }
    JobSystemStats jobSystemStats() const noexcept { return jobs_->stats(); }
    std::span<const SystemStats> systemStats() const noexcept {
        return std::span<const SystemStats>(systemStats_.data(),
                                            systemStats_.size());
    }

    ResourceRegistry&       resources()       noexcept { return resources_; }
    const ResourceRegistry& resources() const noexcept { return resources_; }

    JobSystem& jobs() noexcept { return *jobs_; }

    // Pause / time-scale (§3.4). Time scale multiplies the dt seen by
    // systems; the engine still ticks at fixedStepSeconds wall-clock.
    // Pause makes step()/run() skip the simulation phase entirely.
    void setTimeScale(double scale) noexcept;
    double timeScale() const noexcept { return timeScale_; }
    void setPaused(bool paused) noexcept { paused_.store(paused, std::memory_order_release); }
    bool paused() const noexcept { return paused_.load(std::memory_order_acquire); }

    // Event-channel access (§3.3). Channels are created on first request,
    // keyed by std::type_index. The returned void* points at a stable
    // EventChannel<T>; callers cast back to the same type.
    //
    // factory: heap-allocates a fresh channel of the matching type.
    // deleter: tears it down at engine destruction.
    // drainFn: called at tick-boundary to swap the front/back buffers.
    void* getEventChannelRaw(std::type_index type,
                             void* (*factory)(),
                             void (*deleter)(void*),
                             void (*drainFn)(void*));

private:
    // Applies a command buffer's commands to the world. Single-threaded.
    void commitBuffer(CommandBuffer& cb);

    // Build the back render frame from current world state, then publish it.
    void buildRenderFrame();

    // Submit the currently-published front frame with an overridden alpha.
    // Used by run() to deliver interpolation frames between sim steps,
    // without rebuilding the instance array (world state is unchanged
    // between ticks). Safe to call only on the sim thread.
    void submitInterpolatedFrame(float alpha);

    // Recompute waves_ from systems_'s declared read/write sets. Greedy
    // first-fit in registration order, so within a wave the order is also
    // registration order and ordering between waves is a topological sort.
    void rebuildWaves();

    Config cfg_;
    std::unique_ptr<JobSystem> jobs_;

    World world_;

    std::vector<std::unique_ptr<ISystem>> systems_;
    // Wave schedule: waves_[w] holds indices into systems_, all of whose
    // declared read/write sets are pairwise non-conflicting. Recomputed when
    // a system is registered. Empty waves never exist.
    std::vector<std::vector<std::size_t>> waves_;
    IRenderer* renderer_ = nullptr;
    IGame*     game_     = nullptr;
    Engine*    publicEngine_ = nullptr;

    // §3.1 logger plumbing. logger_ is the user-installed sink (or null,
    // meaning "fall back to defaultLogger_"). defaultLogger_ writes to
    // std::cerr at Warn+. Engine routes lifecycle and loader messages
    // through whichever is active via the logger() accessor.
    ILogger*      logger_ = nullptr;
    DefaultLogger defaultLogger_;

    std::uint64_t tick_ = 0;
    double simulationTime_ = 0.0;

    std::atomic<bool> quit_{false};
    bool initialized_ = false;

    // Pause / time-scale state (§3.4). timeScale_ is only mutated on the
    // sim thread; paused_ is atomic so any thread can flip it (parallels
    // requestQuit()).
    double            timeScale_ = 1.0;
    std::atomic<bool> paused_{false};

    // Type-erased event channel store (§3.3). Owned vector of unique_ptrs
    // gives stable addresses; the type_index map is a lookup index.
    // Drained between steps (preStep → emits stay live, drain happens on
    // tick boundary after postStep).
    struct EventChannelEntry {
        void* ptr               = nullptr;
        void  (*deleter)(void*) = nullptr;
        void  (*drain)(void*)   = nullptr;
    };
    std::unordered_map<std::type_index, EventChannelEntry> eventChannels_;

    // Double-buffered render-frame storage. We build into back_, then
    // atomically publish the pointer; the renderer reads through front_.
    std::array<std::vector<RenderInstance>, 2> renderInstanceBuffers_;
    // §3.2 batch 8: per-frame merged storage backing the hierarchical
    // RenderFrame spans (cameras, lights, per-pass draw items, debug
    // geometry). Double-buffered alongside renderInstanceBuffers_; the
    // RenderFrame::* spans point into renderFrameStorage_[back] during
    // build, and are read by the renderer through renderFrames_[front].
    struct HierarchicalRenderStorage {
        std::vector<Camera>      cameras;
        std::vector<Light>       lights;
        std::array<std::vector<DrawItem>, kRenderPassCount> drawItems;
        std::vector<DebugLine>   debugLines;
        std::vector<DebugPoint>  debugPoints;
        std::vector<DebugText>   debugText;
        void clear() noexcept {
            cameras.clear();
            lights.clear();
            for (auto& bin : drawItems) bin.clear();
            debugLines.clear();
            debugPoints.clear();
            debugText.clear();
        }
    };
    std::array<HierarchicalRenderStorage, 2> renderFrameStorage_;
    std::array<RenderFrame, 2> renderFrames_;
    std::atomic<unsigned> frontIndex_{0};

    // §3.2 batch 8: per-system RenderFrameBuilder, parallel to systems_.
    // Persisted across ticks so the inner std::vector allocations are
    // reused — steady-state usage pays zero allocations after the first
    // tick. Cleared at the start of each tick before invoking each
    // system's buildRenderFrame hook.
    std::vector<RenderFrameBuilder> systemRenderBuilders_;

    // Wall-clock pacing.
    std::chrono::steady_clock::time_point lastIterationTime_{};
    double accumulatedTime_ = 0.0;

    EngineStats stats_;
    std::uint64_t commandsThisStep_ = 0;  // accumulated across commitBuffer calls
    double        commitSecondsThisStep_ = 0.0;  // accumulated in step() across waves

    // Per-system snapshot, one entry per registered system in registration
    // order. Grown by registerSystem; updated at the end of each step().
    std::vector<SystemStats> systemStats_;

    // Engine-owned, thread-safe registry for game-side resources (meshes,
    // textures, audio clips, ...). Lifetime matches the engine.
    ResourceRegistry resources_;

    // §3.3 registered resource loaders. Pumped after the postStep hook
    // commits. Torn down in reverse-registration order during shutdown.
    std::vector<std::unique_ptr<IResourceLoader>> resourceLoaders_;
};

} // namespace threadmaxx::internal
