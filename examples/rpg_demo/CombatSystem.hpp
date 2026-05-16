// §3.11.1 batch D1 — sword-swing damage detection.
//
// Runs after HierarchySystem (which propagates the sword's world
// transform from the player). Reads the sword tip world position, and
// when the player is in the "active hit window" of a swing, queries
// the NPCBrainSystem's spatial hash for hostile NPCs within
// `kSwordTipRadius`. For each hit, emits a `DamageDealt` event on the
// engine's typed event channel; the DamageSystem subscribes and
// applies the HP change in submission order.
//
// Designed to fire damage at most once per swing — we sample the
// player's `swordSwingTimer` against the value set last tick and only
// emit when the timer "armed" from 0 → kSwordSwingSeconds during the
// previous PlayerInputSystem step.

#pragma once

#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

namespace threadmaxx { class Engine; }

namespace rpg {

class NPCBrainSystem;

class CombatSystem : public threadmaxx::ISystem {
public:
    CombatSystem(threadmaxx::Engine* engine,
                 WorldState* worldState,
                 UserComponentIds* ids,
                 const NPCBrainSystem* brain)
        : engine_(engine), worldState_(worldState), ids_(ids), brain_(brain) {}

    const char* name() const noexcept override { return "combat"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform}
             | threadmaxx::ComponentSet{threadmaxx::Component::Faction}
             | threadmaxx::ComponentSet{threadmaxx::Component::Health};
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet::none();
    }

    void update(threadmaxx::SystemContext& ctx) override;

private:
    threadmaxx::Engine*   engine_     = nullptr;
    WorldState*           worldState_ = nullptr;
    UserComponentIds*     ids_        = nullptr;
    const NPCBrainSystem* brain_      = nullptr;
    /// Last-seen swordSwingTimer value; we trigger a damage check only
    /// on the rising edge (was 0, now > 0).
    float                 prevSwingTimer_ = 0.0f;
};

} // namespace rpg
