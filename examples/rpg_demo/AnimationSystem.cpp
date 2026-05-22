#include "AnimationSystem.hpp"

#include "Heightmap.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/internal/Archetype.hpp>

#include <cmath>
#include <utility>
#include <vector>

namespace rpg {

void AnimationSystem::update(threadmaxx::SystemContext& ctx) {
    // 2026-05-20 (rev 2) — preserve the per-entity Y-bob write for
    // gameplay-visible Y, but skip it under `--stress`. In stress
    // mode the CubeRenderSystem applies the bob at DRAW time (cheap,
    // zero command-buffer cost) so the visual is identical; under
    // 100k NPCs the per-tick setTransform writes would have been
    // ~5 ms of commit work for a purely cosmetic effect.
    //
    // Non-stress mode keeps the legacy contract: AnimationSystem
    // writes the bobbed Y into Transform.position.y, so any test or
    // game-side code that reads the transform sees the visual Y.
    if (worldState_ && worldState_->stressMode) return;

    const auto& w = ctx.world();
    const double simTime = static_cast<double>(ctx.tick()) * ctx.dt();
    const auto animId = ids_->animState;
    // §3.11.8 batch D8 — when a heightmap is present, bob around the
    // *current* terrain Y rather than the stale spawn-time `baseY`.
    // Falls back to `a.baseY` when the demo runs without terrain
    // (pre-D8 behavior, useful for headless tests that opt out of
    // heightmap generation).
    const Heightmap* hmap = (worldState_ && worldState_->heightmap)
                                ? worldState_->heightmap.get()
                                : nullptr;

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
            constexpr float kRefSpeed = 4.0f;
            const float ratio = std::min(speed / kRefSpeed, 1.0f);
            const float bob = std::sin(static_cast<float>(simTime) *
                                       a.frequency + a.phase) *
                              a.amplitude * ratio;
            // §3.11.8 batch D8 — terrain-aware bob baseline.
            const float baseline = hmap
                ? hmap->heightAt(tr.position.x, tr.position.z) + tr.scale.y * 0.5f
                : a.baseY;
            threadmaxx::Transform out = tr;
            out.position.y = baseline + bob;
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
