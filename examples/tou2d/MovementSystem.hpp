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

private:
    UserComponentIds ids_;
};

} // namespace tou2d
