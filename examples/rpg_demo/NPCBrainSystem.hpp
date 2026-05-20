#pragma once

#include <threadmaxx/SpatialHash.hpp>
#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

namespace threadmaxx { class Engine; }

namespace rpg {

/// Hostile-faction NPC brain. Each tick:
///   1. `preStep` rebuilds the spatial hash from every alive entity that
///      carries a `BoundingVolume`.
///   2. `update` queries the hash for every NPC and transitions through
///      Idle → Wander → Flee based on player proximity.
/// Writes Velocity on each NPC; emits `DamageDealt` when a hostile is
/// in melee range (NPC-to-player attack, 2026-05-20).
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

    void preStep(threadmaxx::SystemContext& ctx) override;
    void update(threadmaxx::SystemContext& ctx) override;

    /// Borrowed read access for downstream systems (pickup collision).
    const threadmaxx::SpatialHash<threadmaxx::EntityHandle>& spatialHash() const noexcept {
        return hash_;
    }

private:
    threadmaxx::Engine* engine_     = nullptr;
    WorldState*         worldState_ = nullptr;
    UserComponentIds*   ids_        = nullptr;
    threadmaxx::SpatialHash<threadmaxx::EntityHandle> hash_;
};

} // namespace rpg
