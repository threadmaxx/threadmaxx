#pragma once

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

namespace rpg {

class NPCBrainSystem;

/// Reads the spatial hash built by `NPCBrainSystem::preStep`, queries for
/// pickup entities near the player, marks each overlapping pickup with
/// `DisabledTag`, increments the player's `PlayerState.pickups`, and
/// publishes a `PickupCollected` event so the HUD can react next tick.
///
/// Declares writes ∋ DisabledTag is not possible (tag bits don't have
/// dedicated scheduling categories — they ride in `ComponentSet::all`).
/// Instead we declare writes ∋ Velocity (it doesn't actually write
/// velocity, but Tag flips land in the same `ComponentSet::all` bucket
/// without a finer-grained mask). For determinism we just rely on the
/// system landing strictly after NPCBrain via `dependencies()`.
class PickupSystem : public threadmaxx::ISystem {
public:
    PickupSystem(threadmaxx::Engine* engine,
                 WorldState* worldState,
                 UserComponentIds* ids,
                 const NPCBrainSystem* brain)
        : engine_(engine), worldState_(worldState), ids_(ids), brain_(brain) {}

    const char* name() const noexcept override { return "pickup"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::BoundingVolume,
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        // ComponentSet::all forces serial against every other system —
        // pickup flips DisabledTag (in ComponentSet::all by virtue of
        // being a tag bit) and rewrites the player's user-component.
        return threadmaxx::ComponentSet::all();
    }

    void update(threadmaxx::SystemContext& ctx) override;

private:
    threadmaxx::Engine*   engine_     = nullptr;
    WorldState*           worldState_ = nullptr;
    UserComponentIds*     ids_        = nullptr;
    const NPCBrainSystem* brain_      = nullptr;
};

} // namespace rpg
