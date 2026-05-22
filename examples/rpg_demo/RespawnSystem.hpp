// §3.11.1 batch D1 — converts dead NPCs into loot drops.
//
// Subscribes to `EntityDied` events via the engine's typed event
// channel. For each death:
//   - Adds `DisabledTag` to the corpse (renderer skips DisabledTag
//     entities, so the dead NPC visibly disappears).
//   - Spawns a gold pickup at the death position via a reserved
//     handle taken from the engine. The pickup carries the standard
//     Pickup user-component value so PickupSystem can consume it.
//
// Uses `Engine::reserveEntityHandle` (§3.5) inside `preStep` so the
// freshly-reserved handle is available before any commit. The actual
// spawn lives in `update`, batched into one `single()` callback.
//
// 2026-05-22 audit (round 2) — also owns the player-death respawn
// pipeline. When the player's `EntityDied` event fires, the system
// records the death timestamp in `WorldState::playerDeathTime`
// (suppressing the loot drop / corpse tag for the player handle)
// and disables the sword. After `kRespawnDelaySeconds` of sim time
// it teleports the player back to `WorldState::playerSpawnPos`,
// restores HP to max, re-enables the sword, and resets the
// transient `PlayerState` motion fields. The NPC brain treats the
// dead player as out-of-AoI during the delay (`NPCBrainSystem`
// 2026-05-22 round-2 change), so combatants disengage and the
// world is calm when the player reappears.

#pragma once

#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

#include <vector>

namespace threadmaxx { class Engine; }

namespace rpg {

class RespawnSystem : public threadmaxx::ISystem {
public:
    RespawnSystem(threadmaxx::Engine* engine,
                  WorldState* worldState,
                  UserComponentIds* ids)
        : engine_(engine), worldState_(worldState), ids_(ids) {}

    const char* name() const noexcept override { return "respawn"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet::none();
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        // Spawns new entities AND flips DisabledTag — force serial.
        return threadmaxx::ComponentSet::all();
    }

    void preStep(threadmaxx::SystemContext& ctx) override;
    void update (threadmaxx::SystemContext& ctx) override;

private:
    struct DropPlan {
        threadmaxx::EntityHandle corpse;
        threadmaxx::EntityHandle pickup;  // reserved up front
        float                    posX = 0.0f, posY = 0.0f, posZ = 0.0f;
    };
    threadmaxx::Engine* engine_     = nullptr;
    WorldState*         worldState_ = nullptr;
    UserComponentIds*   ids_        = nullptr;
    /// Per-tick drop plan; cleared at the start of preStep.
    std::vector<DropPlan> drops_;
    /// 2026-05-22 audit fix — set in `preStep` when the player is
    /// the target of an `EntityDied` event. `update` then disables
    /// the worldState_->sword in the same tick's commit phase.
    bool                  disableSwordOnDeath_ = false;
    /// 2026-05-22 audit (round 3) — set alongside `disableSwordOnDeath_`
    /// when the player dies. `update` adds `DisabledTag` to the player
    /// so the renderer hides the corpse (instead of leaving it in the
    /// world frozen mid-action). The respawn pump removes the tag
    /// alongside the teleport / heal.
    bool                  disablePlayerOnDeath_ = false;
};

} // namespace rpg
