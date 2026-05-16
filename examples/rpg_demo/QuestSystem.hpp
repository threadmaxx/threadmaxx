// §3.11.4 batch D4 — quest tracking via typed event channels.
//
// Subscribes to `PickupCollected` and `EntityDied` on construction;
// the callbacks update the per-quest `progress` counter stored on
// `WorldState::quests`. When a quest advances, the system emits a
// `QuestProgressed` event on the engine's typed channel; HudSystem
// subscribes to print quest progress lines.
//
// All work happens inside event callbacks fired during the engine's
// tick-boundary drain — no per-tick `update` loop. The `ISystem`
// interface still requires reads/writes masks; we declare them empty
// so the wave scheduler doesn't serialize anything around us.

#pragma once

#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

namespace threadmaxx { class Engine; }

namespace rpg {

class QuestSystem : public threadmaxx::ISystem {
public:
    QuestSystem(threadmaxx::Engine* engine, WorldState* worldState);
    ~QuestSystem() override;

    const char* name() const noexcept override { return "quest"; }
    threadmaxx::ComponentSet reads()  const noexcept override { return threadmaxx::ComponentSet::none(); }
    threadmaxx::ComponentSet writes() const noexcept override { return threadmaxx::ComponentSet::none(); }
    void update(threadmaxx::SystemContext&) override {}

private:
    threadmaxx::Engine*      engine_     = nullptr;
    WorldState*              worldState_ = nullptr;
    threadmaxx::Subscription pickupSub_;
    threadmaxx::Subscription deathSub_;
};

} // namespace rpg
