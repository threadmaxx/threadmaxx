#include "ParticleSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/internal/Archetype.hpp>

#include <vector>

namespace rpg {

void ParticleSystem::update(threadmaxx::SystemContext& ctx) {
    const auto& w = ctx.world();
    const auto particleBit = ids_->particle.componentBit();
    const auto chunkCount  = w.archetypeChunkCount();
    if (chunkCount == 0) return;

    // §3.11.9 batch D9 — `simulationTime()` is the wall-clock-equivalent
    // we baked into each Particle's `spawnTimeSeconds` at emit time.
    // `tick() * dt` reproduces it without needing the Engine* indirection.
    const float simTime = static_cast<float>(ctx.tick()) *
                          static_cast<float>(ctx.dt());

    // Stage expired handles before scheduling `destroy`. The
    // per-particle work is one float subtract and one comparison;
    // staging keeps the engine command-buffer write loop tight.
    std::vector<threadmaxx::EntityHandle> expired;
    expired.reserve(64);

    for (std::size_t c = 0; c < chunkCount; ++c) {
        const auto& chunk = w.archetypeChunk(c);
        if (!chunk.mask.has(particleBit)) continue;
        const auto rows = chunk.entities.size();
        if (rows == 0) continue;
        const auto span = threadmaxx::user::chunkSpan<Particle>(
            chunk, ids_->particle);
        if (span.empty()) continue;
        for (std::size_t r = 0; r < rows; ++r) {
            const auto& p = span[r];
            if (simTime - p.spawnTimeSeconds >= p.initialLifetime) {
                expired.push_back(chunk.entities[r]);
            }
        }
    }

    if (expired.empty()) return;

    ctx.single([expired = std::move(expired)]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        for (auto h : expired) cb.destroy(h);
    });
}

} // namespace rpg
