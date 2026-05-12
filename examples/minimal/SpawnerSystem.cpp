#include "SpawnerSystem.hpp"

#include <threadmaxx/World.hpp>

std::uint32_t SpawnerSystem::xorshift() {
    std::uint64_t x = rngState_;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rngState_ = x;
    return static_cast<std::uint32_t>(x);
}

void SpawnerSystem::update(threadmaxx::SystemContext& ctx) {
    const std::uint64_t t = ctx.tick();

    // Spawn three new entities every spawnEvery_ ticks.
    if (t % spawnEvery_ == 0) {
        ctx.single([this, t](threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
            for (int i = 0; i < 3; ++i) {
                const float vx = (static_cast<float>(xorshift() & 0xFFFF) / 32768.0f) - 1.0f;
                const float vz = (static_cast<float>(xorshift() & 0xFFFF) / 32768.0f) - 1.0f;
                threadmaxx::Transform tr;
                tr.position = {0.0f, 0.0f, 0.0f};
                threadmaxx::Velocity v;
                v.linear = {vx, 0.0f, vz};
                threadmaxx::RenderTag tag;
                tag.meshId = 1;
                tag.materialId = 0;
                cb.spawn(tr, v, tag, threadmaxx::UserData{static_cast<std::uint64_t>(t)});
            }
        });
    }

    // Reap entities that drifted past +/- 50 units on the XZ plane.
    const auto& world = ctx.world();
    const auto entities   = world.entities();
    const auto transforms = world.transforms();
    const auto count = static_cast<std::uint32_t>(entities.size());

    ctx.parallelFor(count, 0,
        [=](threadmaxx::Range r, threadmaxx::CommandBuffer& cb) {
            for (std::uint32_t i = r.begin; i < r.end; ++i) {
                const auto& p = transforms[i].position;
                if (p.x < -50.0f || p.x > 50.0f ||
                    p.z < -50.0f || p.z > 50.0f) {
                    cb.destroy(entities[i]);
                }
            }
        });
}
