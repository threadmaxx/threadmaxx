#pragma once

#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

namespace rpg {

/// §3.11 batch D8 — snaps mover Y to the terrain.
///
/// Iterates entities that carry `Transform + Velocity` (i.e. things
/// that move under their own steam — player and NPCs; not terrain
/// tiles, not pickups, not the Parent-attached sword) and writes
/// `Transform.position.y = heightAt(x, z) + scale.y * 0.5`. Pure
/// cosmetic / gameplay correction; doesn't touch the simulation
/// elsewhere.
///
/// Registered between `MovementSystem` (which integrates x/z) and
/// `AnimationSystem` (which computes the cosmetic Y-bob) so the bob
/// oscillates around the just-applied terrain Y. In stress mode
/// `AnimationSystem` is a no-op; this system is what keeps NPCs on
/// the ground.
///
/// No-ops if `worldState_->heightmap` is null — useful for headless
/// tests that pre-empt heightmap generation by zeroing the field
/// before `engine.initialize()`.
class TerrainAttachSystem : public threadmaxx::ISystem {
public:
    explicit TerrainAttachSystem(const WorldState* worldState) noexcept;

    const char* name() const noexcept override { return "terrain-attach"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Velocity,  // presence-only filter
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{threadmaxx::Component::Transform};
    }
    void update(threadmaxx::SystemContext& ctx) override;

private:
    const WorldState* worldState_ = nullptr;
};

} // namespace rpg
