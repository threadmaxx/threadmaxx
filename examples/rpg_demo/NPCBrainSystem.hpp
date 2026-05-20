#pragma once

#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

namespace threadmaxx { class Engine; }

namespace rpg {

/// Hostile-faction NPC brain. Each tick:
///   1. `update` walks Faction-bearing chunks (skipping pickup chunks)
///      in parallel and runs the per-NPC state machine.
///   2. Writes Velocity + NpcState only when they actually change
///      (skip-when-equal heuristic — most idle NPCs avoid the command-
///      buffer write entirely).
///   3. Emits `DamageDealt` events for hostile NPCs that strike the
///      player.
///
/// 2026-05-20 (rev 2): `preStep` (SpatialHash build) was REMOVED.
/// CombatSystem owns its own per-swing chunk scan now; no other
/// system queried the hash. Saves ~10 ms / tick at 100k NPCs.
class NPCBrainSystem : public threadmaxx::ISystem {
public:
    NPCBrainSystem(threadmaxx::Engine* engine,
                   WorldState* worldState, UserComponentIds* ids);

    const char* name() const noexcept override { return "npc-brain"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Faction,
            threadmaxx::Component::BoundingVolume,
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Velocity};
    }

    void update(threadmaxx::SystemContext& ctx) override;

private:
    threadmaxx::Engine* engine_     = nullptr;
    WorldState*         worldState_ = nullptr;
    UserComponentIds*   ids_        = nullptr;
};

} // namespace rpg
