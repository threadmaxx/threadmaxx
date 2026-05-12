#include "MovementSystem.hpp"

#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>

void MovementSystem::update(threadmaxx::SystemContext& ctx) {
    const auto dt = static_cast<float>(ctx.dt());
    threadmaxx::forEach<threadmaxx::Transform, threadmaxx::Velocity>(ctx,
        [dt](threadmaxx::EntityHandle e,
             const threadmaxx::Transform& t,
             const threadmaxx::Velocity& v,
             threadmaxx::CommandBuffer& cb) {
            threadmaxx::Transform next = t;
            next.position = t.position + v.linear * dt;
            cb.setTransform(e, next);
        });
}
