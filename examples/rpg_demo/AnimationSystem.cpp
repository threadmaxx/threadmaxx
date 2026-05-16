#include "AnimationSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <cmath>
#include <vector>

namespace rpg {

void AnimationSystem::update(threadmaxx::SystemContext& ctx) {
    const auto& w = ctx.world();
    // Simulation time = tick × fixed dt. Deterministic across runs.
    const double simTime = static_cast<double>(ctx.tick()) * ctx.dt();
    const auto animId = ids_->animState;

    // Two-pass: snapshot every (entity, new Y) tuple inside
    // `update` (parallel-safe reads), then write them all in a
    // single `single()` callback. Keeps the command buffer flow
    // simple and avoids inflating the per-entity callback signature.
    struct PendingY {
        threadmaxx::EntityHandle e;
        threadmaxx::Transform    t;
    };
    std::vector<PendingY> pending;

    const auto chunkCount = w.archetypeChunkCount();
    for (std::size_t i = 0; i < chunkCount; ++i) {
        const auto& chunk = w.archetypeChunk(i);
        if (!chunk.mask.has(animId.componentBit())) continue;
        if (!chunk.mask.has(threadmaxx::Component::Transform)) continue;
        if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;

        const bool hasVel = chunk.mask.has(threadmaxx::Component::Velocity);
        auto animSpan = threadmaxx::user::chunkSpan<AnimState>(chunk, animId);

        const auto n = chunk.entities.size();
        for (std::size_t r = 0; r < n; ++r) {
            const auto& tr  = chunk.transforms[r];
            const auto& a   = animSpan[r];
            float speed = 0.0f;
            if (hasVel) {
                const auto& v = chunk.velocities[r];
                speed = std::sqrt(v.linear.x * v.linear.x +
                                  v.linear.z * v.linear.z);
            }
            // Speed ratio: 0 at rest, ~1 at typical wander/charge
            // speed. Clamped so a fast-flee NPC doesn't bob to the
            // ceiling.
            constexpr float kRefSpeed = 4.0f;
            const float ratio = std::min(speed / kRefSpeed, 1.0f);
            const float bob = std::sin(static_cast<float>(simTime) *
                                       a.frequency + a.phase) *
                              a.amplitude * ratio;
            threadmaxx::Transform out = tr;
            out.position.y = a.baseY + bob;
            // Only schedule a write if Y actually changed; saves
            // command-buffer churn when the entity is at rest.
            if (out.position.y != tr.position.y) {
                pending.push_back({chunk.entities[r], out});
            }
        }
    }

    if (pending.empty()) return;
    ctx.single([writes = std::move(pending)]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        for (const auto& p : writes) cb.setTransform(p.e, p.t);
    });
}

} // namespace rpg
