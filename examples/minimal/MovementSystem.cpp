#include "MovementSystem.hpp"

#include <threadmaxx/World.hpp>

void MovementSystem::update(threadmaxx::SystemContext& ctx) {
    const auto& world = ctx.world();
    const auto entities = world.entities();
    const auto transforms = world.transforms();
    const auto velocities = world.velocities();
    const auto dt = static_cast<float>(ctx.dt());

    const auto count = static_cast<std::uint32_t>(entities.size());

    ctx.parallelFor(count, /*grain*/ 0,
        [=](threadmaxx::Range r, threadmaxx::CommandBuffer& cb) {
            for (std::uint32_t i = r.begin; i < r.end; ++i) {
                const auto& t = transforms[i];
                const auto& v = velocities[i];
                threadmaxx::Transform next = t;
                next.position = t.position + v.linear * dt;
                cb.setTransform(entities[i], next);
            }
        });
}
