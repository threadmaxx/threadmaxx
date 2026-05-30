#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

#include <cstdint>

namespace tou2d {

class ParticleSystem;
struct TerrainGrid;

/// Reads PlayerInput + Transform + Velocity per ship, applies thrust
/// (turn rate to orientation, forward force to velocity), then folds
/// in level gravity + air resistance. M1 has no level config so the
/// tunables are hard-coded (see `MovementSystem.cpp` constants).
///
/// World-space convention used by the whole 2D project:
///   * +X = right, +Y = up (gravity is -Y), +Z = out of screen
///   * Ship-local "forward" = +Y; orientation = rotation around Z
///   * Positive turn = CCW (left)
///
/// reads / writes:
///   * reads  = {Transform, Velocity, UserData}
///   * writes = {Transform, Velocity}
/// — Transform/Velocity overlap with the read set is intentional: we
///   integrate position from velocity inside the same wave.
class MovementSystem : public threadmaxx::ISystem {
public:
    explicit MovementSystem(UserComponentIds ids) noexcept;

    const char*              name()   const noexcept override { return "tou2d.movement"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Velocity,
            threadmaxx::Component::UserData,
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Velocity,
        };
    }

    void update(threadmaxx::SystemContext& ctx) override;

    /// Hard X/Y clamp applied to every ship after thrust + gravity
    /// integration. The ship's AABB is kept fully inside the rect
    /// `[minX + shipHalf, maxX - shipHalf]` in X (same in Y); the
    /// offending velocity axis is zeroed on contact. The rect is
    /// passed in directly so an asymmetric cell-grid extent (which
    /// happens whenever cellsX or cellsY is even) is honored.
    /// Pass-through default leaves bounds disabled.
    void setLevelRect(float minX, float minY,
                      float maxX, float maxY) noexcept {
        levelMinX_   = minX;
        levelMinY_   = minY;
        levelMaxX_   = maxX;
        levelMaxY_   = maxY;
        levelActive_ = (maxX > minX) && (maxY > minY);
    }

    /// M7.3 §5.1 — borrowed pointer to the demo's ParticleSystem.
    /// When set, every Nth tick (`kThrustEmitInterval`) each ship
    /// whose `PlayerInput::thrust > 0` emits one Thrust particle
    /// behind the engine. Null is fine — headless tests don't wire
    /// particles and `update()` then just skips the emit.
    void setParticleSystem(ParticleSystem* p) noexcept { particles_ = p; }

    /// M7.6 — borrowed pointer to the demo's TerrainGrid. When set,
    /// `update()` samples each ship's center + 4 cardinal neighbors
    /// (at one ship-half offset) for `Attribute::Water`; the resulting
    /// 0..1 wetness fraction blends buoyancy and per-tick drag into
    /// the normal gravity / damping integrate. Null is fine — without
    /// a grid, water mechanics are a no-op and behaviour collapses
    /// to the pre-M7.6 air-only path.
    void setTerrainGrid(const TerrainGrid* g) noexcept { terrain_ = g; }

    /// M7.3 §5.1 — emit one thruster puff every `kThrustEmitInterval`
    /// ticks per actively-thrusting ship. 3 → 20 puffs/sec at 60 Hz;
    /// combined with TTL 12-18 each ship caps at ~6 live thrust
    /// particles in the 256-particle pool.
    static constexpr std::uint32_t kThrustEmitInterval = 3;

private:
    UserComponentIds ids_;
    float            levelMinX_   = 0.0f;
    float            levelMinY_   = 0.0f;
    float            levelMaxX_   = 0.0f;
    float            levelMaxY_   = 0.0f;
    bool             levelActive_ = false;
    ParticleSystem*    particles_ = nullptr;
    const TerrainGrid* terrain_   = nullptr;   // M7.6 — water lookup; nullable
    std::uint32_t      tickPhase_ = 0;   // M7.3 — bumped each update() call
};

} // namespace tou2d
