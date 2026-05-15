#pragma once

#include <threadmaxx/Game.hpp>

#include "DemoTypes.hpp"

namespace rpg {

class NPCBrainSystem;

/// IGame implementation. Owns the shared `WorldState` + the registered
/// `UserComponentIds`. Spawns terrain + player + N NPCs + M pickups in
/// `onSetup` and registers every system in dependency order.
class DemoGame : public threadmaxx::IGame {
public:
    DemoGame();
    ~DemoGame() override;

    void onSetup(threadmaxx::Engine& engine,
                 threadmaxx::World&  world,
                 threadmaxx::CommandBuffer& seed) override;

    WorldState&       worldState()       noexcept { return worldState_; }
    const WorldState& worldState() const noexcept { return worldState_; }

private:
    WorldState        worldState_;
    UserComponentIds  ids_;
    // Owned externally by the engine after `registerSystem` but cached
    // as a raw pointer so PickupSystem can read the spatial hash.
    NPCBrainSystem*   brain_ = nullptr;
};

} // namespace rpg
