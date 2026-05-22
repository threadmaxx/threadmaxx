#pragma once

#include <threadmaxx/Components.hpp>
#include <threadmaxx/System.hpp>

#include "DemoTypes.hpp"

#include <cstdint>
#include <vector>

namespace threadmaxx { class Engine; }

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
    /// 2026-05-22 audit refactor — also takes UserComponentIds so the
    /// jump-landing path can write PlayerState back via the user-
    /// component pipe. Pre-refactor TerrainAttach unconditionally
    /// snapped player Y to ground, making the Space jump invisible.
    /// 2026-05-22 audit (round 3) — optionally takes an Engine* so it
    /// can emit `DamageDealt` events for entities that cross the
    /// terrain edge (`±kFallDeathHalfExtent` in XZ or below
    /// `kFallDeathFloorY` in Y). When null (headless tests) the
    /// fall-death path silently no-ops.
    TerrainAttachSystem(const WorldState* worldState,
                        UserComponentIds* ids = nullptr,
                        threadmaxx::Engine* engine = nullptr) noexcept;

    const char* name() const noexcept override { return "terrain-attach"; }
    threadmaxx::ComponentSet reads() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Velocity,  // presence-only filter
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        // Writes Transform and (on landing) PlayerState; force
        // serial against PlayerInputSystem so the jump-state
        // handoff is deterministic.
        return threadmaxx::ComponentSet::all();
    }
    void update(threadmaxx::SystemContext& ctx) override;

private:
    const WorldState*   worldState_ = nullptr;
    UserComponentIds*   ids_        = nullptr;
    threadmaxx::Engine* engine_     = nullptr;

    /// Per-entity previous XZ ground-pos cache for the step-up rule.
    /// 2026-05-22 (round 9, voxel pivot) — when an entity tries to
    /// walk into a cell whose quantized height exceeds its current
    /// ground height by more than `kStepUpMax`, we revert its XZ to
    /// `prevPos_[idx].pos` so it stops at the wall instead of
    /// teleporting on top. The generation field guards against
    /// reusing a stale entry after destroy+respawn (entity index
    /// gets recycled but generation bumps).
    struct PrevSlot {
        threadmaxx::Vec3 pos;
        std::uint32_t    generation = 0;
        bool             valid      = false;
    };
    std::vector<PrevSlot> prevPos_;
};

} // namespace rpg
