#pragma once

#include "JobSystem.hpp"
#include "WorldImpl.hpp"

#include "threadmaxx/CommandBuffer.hpp"
#include "threadmaxx/Config.hpp"
#include "threadmaxx/RenderFrame.hpp"
#include "threadmaxx/Stats.hpp"
#include "threadmaxx/System.hpp"
#include "threadmaxx/World.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
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
    void single(JobFn fn) override;

    // Per-system list of command buffers, in submission order.
    std::vector<CommandBuffer>& buffers() noexcept { return buffers_; }

    // Number of jobs handed to JobSystem from this context. parallelFor
    // chunks count; single() does not (it runs inline).
    std::uint64_t jobsSubmitted() const noexcept { return jobsSubmitted_; }

private:
    class EngineImpl& engine_;
    const World&      world_;
    double            dt_;
    std::uint64_t     tick_;
    std::vector<CommandBuffer> buffers_;
    std::uint64_t     jobsSubmitted_ = 0;
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
    void setRenderer(IRenderer* renderer) noexcept { renderer_ = renderer; }

    World&        world()        noexcept { return world_; }
    const World&  world() const  noexcept { return world_; }
    const Config& config() const noexcept { return cfg_; }
    std::uint64_t tick()   const noexcept { return tick_; }
    double simulationTime() const noexcept { return simulationTime_; }

    EngineStats stats() const noexcept { return stats_; }
    std::span<const SystemStats> systemStats() const noexcept {
        return std::span<const SystemStats>(systemStats_.data(),
                                            systemStats_.size());
    }

    JobSystem& jobs() noexcept { return *jobs_; }

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

    std::uint64_t tick_ = 0;
    double simulationTime_ = 0.0;

    std::atomic<bool> quit_{false};
    bool initialized_ = false;

    // Double-buffered render-frame storage. We build into back_, then
    // atomically publish the pointer; the renderer reads through front_.
    std::array<std::vector<RenderInstance>, 2> renderInstanceBuffers_;
    std::array<RenderFrame, 2> renderFrames_;
    std::atomic<unsigned> frontIndex_{0};

    // Wall-clock pacing.
    std::chrono::steady_clock::time_point lastIterationTime_{};
    double accumulatedTime_ = 0.0;

    EngineStats stats_;
    std::uint64_t commandsThisStep_ = 0;  // accumulated across commitBuffer calls

    // Per-system snapshot, one entry per registered system in registration
    // order. Grown by registerSystem; updated at the end of each step().
    std::vector<SystemStats> systemStats_;
};

} // namespace threadmaxx::internal
