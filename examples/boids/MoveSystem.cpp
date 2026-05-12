#include "MoveSystem.hpp"

#include "BoidsConfig.hpp"

#include <threadmaxx/World.hpp>

#include <cstdint>

namespace {
inline float wrap(float v, float lo, float hi) {
    const float w = hi - lo;
    while (v <  lo) v += w;
    while (v >= hi) v -= w;
    return v;
}
} // namespace

void BoidsMoveSystem::update(threadmaxx::SystemContext& ctx) {
    const auto entities   = ctx.world().entities();
    const auto transforms = ctx.world().transforms();
    const auto velocities = ctx.world().velocities();
    const auto dt         = static_cast<float>(ctx.dt());

    const auto N = static_cast<std::uint32_t>(entities.size());
    if (N == 0) return;

    ctx.parallelFor(N, /*grain*/ 64,
        [=](threadmaxx::Range r, threadmaxx::CommandBuffer& cb) {
            for (auto i = r.begin; i < r.end; ++i) {
                threadmaxx::Transform next = transforms[i];
                next.position.x = wrap(next.position.x + velocities[i].linear.x * dt,
                                       0.0f, boids::kWindowW);
                next.position.z = wrap(next.position.z + velocities[i].linear.z * dt,
                                       0.0f, boids::kWindowH);
                cb.setTransform(entities[i], next);
            }
        });
}
