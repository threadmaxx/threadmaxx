#pragma once

#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/System.hpp>
#include <threadmaxx/Telemetry.hpp>

#include "DemoTypes.hpp"

#include <functional>
#include <memory>

namespace rpg {

/// Sim-thread HUD telemetry consumer.
///   - `preStep`: polls the F1 edge to toggle a Chrome-trace
///     `FileTraceSink`.
///   - subscribes to `PickupCollected` and prints summary lines.
///   - `postStep`: writes the most recent stats to `stdout` every N
///     ticks.
class HudSystem : public threadmaxx::ISystem {
public:
    /// §3.11 batch 9b.3 — optional callback invoked on F12. The demo
    /// installs a lambda that forwards to
    /// `VulkanRenderer::reloadShaders`; tests leave it null and F12
    /// becomes a no-op.
    using ReloadShadersFn = std::function<void()>;

    HudSystem(threadmaxx::Engine* engine,
              WorldState* worldState,
              UserComponentIds* ids,
              ReloadShadersFn reloadShadersFn = {});
    ~HudSystem() override;

    const char* name() const noexcept override { return "hud"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
    bool skippable() const noexcept override { return true; }
    void update(threadmaxx::SystemContext&) override {}
    void preStep(threadmaxx::SystemContext& ctx) override;
    void postStep(threadmaxx::SystemContext& ctx) override;

private:
    threadmaxx::Engine*        engine_     = nullptr;
    WorldState*                worldState_ = nullptr;
    UserComponentIds*          ids_        = nullptr;
    threadmaxx::Subscription   pickupSub_;
    /// §3.11.1 batch D1 — kill counter.
    threadmaxx::Subscription   deathSub_;
    /// §3.11.5 batch D5 — skip / budget telemetry.
    threadmaxx::Subscription   skippedSub_;
    threadmaxx::Subscription   budgetSub_;
    /// §3.11.4 batch D4 — quest progress tracker.
    threadmaxx::Subscription   questSub_;
    /// §3.11.7 batch D7 — asset reload + reload counter.
    threadmaxx::Subscription   reloadSub_;
    std::unique_ptr<threadmaxx::FileTraceSink> trace_;
    std::uint64_t              lastLoggedTick_ = 0;
    /// §3.11 batch 9b.3 — null when no renderer is wired (headless
    /// tests) or when main.cpp didn't provide one.
    ReloadShadersFn            reloadShadersFn_;
};

} // namespace rpg
