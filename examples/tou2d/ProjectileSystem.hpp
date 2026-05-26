#pragma once

#include "DemoTypes.hpp"

#include <threadmaxx/System.hpp>

namespace tou2d {

/// Integrates Bullet kinematics: position += velocity * dt; ttl -= dt;
/// despawn at ttl ≤ 0. Bullets do not feel gravity (consistent with
/// the original TOU's Dumbfire). Terrain-contact destruction is the
/// BulletTerrainSystem's job (next wave).
///
/// reads / writes:
///   * reads  = {Transform, Velocity}
///   * writes = {Transform, EntityStructural}  — write Transform for
///              integration; structural for ttl-expiry destroy.
class ProjectileSystem : public threadmaxx::ISystem {
public:
    explicit ProjectileSystem(UserComponentIds ids) noexcept;

    const char*              name()   const noexcept override { return "tou2d.projectile"; }
    threadmaxx::ComponentSet reads()  const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::Velocity,
        };
    }
    threadmaxx::ComponentSet writes() const noexcept override {
        return threadmaxx::ComponentSet{
            threadmaxx::Component::Transform,
            threadmaxx::Component::EntityStructural,
        };
    }

    void update(threadmaxx::SystemContext& ctx) override;

    /// Bullets whose centroid leaves this rect are destroyed on the
    /// next tick — keeps them from flying off into the void past the
    /// level extent (the original TOU's bullets self-terminate at the
    /// boundary). Same shape as `MovementSystem::setLevelRect` so the
    /// host can compute the rect once and pass it to both.
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
