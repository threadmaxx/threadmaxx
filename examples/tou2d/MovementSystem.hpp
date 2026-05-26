#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

namespace tou2d {

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

private:
    UserComponentIds ids_;
    float            levelMinX_   = 0.0f;
    float            levelMinY_   = 0.0f;
    float            levelMaxX_   = 0.0f;
    float            levelMaxY_   = 0.0f;
    bool             levelActive_ = false;
};

} // namespace tou2d
