#pragma once

#include "JobSystem.hpp"
#include "WorldImpl.hpp"

#include "threadmaxx/CommandBuffer.hpp"
#include "threadmaxx/Config.hpp"
#include "threadmaxx/RenderFrame.hpp"
#include "threadmaxx/System.hpp"
#include "threadmaxx/World.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
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

private:
    class EngineImpl& engine_;
    const World&      world_;
    double            dt_;
    std::uint64_t     tick_;
    std::vector<CommandBuffer> buffers_;
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

    JobSystem& jobs() noexcept { return *jobs_; }

private:
    // Applies a command buffer's commands to the world. Single-threaded.
    void commitBuffer(CommandBuffer& cb);

    // Build the back render frame from current world state, then publish it.
    void buildRenderFrame();

    Config cfg_;
    std::unique_ptr<JobSystem> jobs_;

    World world_;

    std::vector<std::unique_ptr<ISystem>> systems_;
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
};

} // namespace threadmaxx::internal
