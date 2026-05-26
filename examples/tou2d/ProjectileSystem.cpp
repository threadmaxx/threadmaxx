#include "ProjectileSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

namespace tou2d {

ProjectileSystem::ProjectileSystem(UserComponentIds ids) noexcept : ids_(ids) {}

void ProjectileSystem::update(threadmaxx::SystemContext& ctx) {
    const auto idsBl = ids_.bullet;
    if (!idsBl.valid()) return;

    const float dt = static_cast<float>(ctx.dt());

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& cb) {
        const auto& view = ctx.worldView();
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsBl.componentBit()))               continue;
            if (!chunk.mask.has(threadmaxx::Component::Transform))   continue;
            if (!chunk.mask.has(threadmaxx::Component::Velocity))    continue;

            const auto blSpan = threadmaxx::user::chunkSpan<Bullet>(chunk, idsBl);
            const auto entities    = chunk.entities;
            const auto& positions  = chunk.transforms;
            const auto& velocities = chunk.velocities;
            const std::size_t n    = entities.size();

            for (std::size_t row = 0; row < n; ++row) {
                Bullet blt = blSpan[row];
                blt.ttlSeconds -= dt;
                if (blt.ttlSeconds <= 0.0f) {
                    cb.destroy(entities[row]);
                    continue;
                }

                threadmaxx::Transform t = positions[row];
                const auto& v = velocities[row];
                t.position.x += v.linear.x * dt;
                t.position.y += v.linear.y * dt;

                // Out-of-bounds → destroy. Bullets that exit the level
                // extent are gone — they never come back, no need to
                // ride out the ttl off-screen.
                if (levelActive_ &&
                    (t.position.x < levelMinX_ || t.position.x > levelMaxX_ ||
                     t.position.y < levelMinY_ || t.position.y > levelMaxY_)) {
                    cb.destroy(entities[row]);
                    continue;
                }

                cb.setTransform(entities[row], t);

                // ttl write-back via addUserComponent (sets-if-present
                // path for the bullet component; the bit is already
                // attached).
                threadmaxx::addUserComponent(cb, idsBl, entities[row], blt);
            }
        }
    });
}

} // namespace tou2d
