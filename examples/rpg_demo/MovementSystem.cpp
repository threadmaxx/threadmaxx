#include "MovementSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

namespace rpg {

void MovementSystem::update(threadmaxx::SystemContext& ctx) {
    const auto& w = ctx.world();
    const float dt = static_cast<float>(ctx.dt());

    threadmaxx::forEachWith<threadmaxx::Transform, threadmaxx::Velocity>(
        ctx,
        [dt, &w](threadmaxx::EntityHandle e,
                 const threadmaxx::Transform& t,
                 const threadmaxx::Velocity&  v,
                 threadmaxx::CommandBuffer&   cb) {
            if (w.hasTag(e, threadmaxx::Component::DisabledTag)) return;
            threadmaxx::Transform out = t;
            out.position.x += v.linear.x * dt;
            out.position.y += v.linear.y * dt;
            out.position.z += v.linear.z * dt;
            cb.setTransform(e, out);
        });
}

} // namespace rpg
