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

    /// §3.11 batch D-audit — exposes the registered user-component
    /// ids. Tests use this to read / write user components without
    /// re-registering them. Pre-batch-D-audit this was internal-only.
    UserComponentIds&       ids()       noexcept { return ids_; }
    const UserComponentIds& ids() const noexcept { return ids_; }

private:
    WorldState        worldState_;
    UserComponentIds  ids_;
    // Owned externally by the engine after `registerSystem` but cached
    // as a raw pointer so PickupSystem can read the spatial hash.
    NPCBrainSystem*   brain_ = nullptr;
};

} // namespace rpg
