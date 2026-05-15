#pragma once

#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/System.hpp>
#include <threadmaxx/Telemetry.hpp>

#include "DemoTypes.hpp"

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
    HudSystem(threadmaxx::Engine* engine,
              WorldState* worldState,
              UserComponentIds* ids);
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
    std::unique_ptr<threadmaxx::FileTraceSink> trace_;
    std::uint64_t              lastLoggedTick_ = 0;
};

} // namespace rpg
