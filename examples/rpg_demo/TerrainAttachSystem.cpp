#include "TerrainAttachSystem.hpp"

#include "Heightmap.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/System.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/internal/Archetype.hpp>

#include <utility>
#include <vector>

namespace rpg {

TerrainAttachSystem::TerrainAttachSystem(const WorldState* worldState) noexcept
    : worldState_(worldState) {}

void TerrainAttachSystem::update(threadmaxx::SystemContext& ctx) {
    if (!worldState_) return;
    const Heightmap* hmap = worldState_->heightmap.get();
    if (!hmap) return;

    const auto& w = ctx.world();
    const auto chunkCount = w.archetypeChunkCount();
    if (chunkCount == 0) return;

    // The system iterates chunks that carry both Transform AND
    // Velocity. Terrain tiles (Transform but no Velocity) and the
    // sword (Transform + Parent + no Velocity) are skipped by the
    // mask test below.

    struct Pending {
        threadmaxx::EntityHandle e;
        threadmaxx::Transform    t;
    };
    std::vector<Pending> writes;

    for (std::size_t i = 0; i < chunkCount; ++i) {
        const auto& chunk = w.archetypeChunk(i);
        if (!chunk.mask.has(threadmaxx::Component::Transform)) continue;
        if (!chunk.mask.has(threadmaxx::Component::Velocity))  continue;
        if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;

        const auto n = chunk.entities.size();
        for (std::size_t r = 0; r < n; ++r) {
            const auto& tr = chunk.transforms[r];
            const float halfY = tr.scale.y * 0.5f;
            const float groundY = hmap->heightAt(tr.position.x, tr.position.z) + halfY;
            if (tr.position.y == groundY) continue;
            threadmaxx::Transform out = tr;
            out.position.y = groundY;
            writes.push_back({chunk.entities[r], out});
        }
    }

    if (writes.empty()) return;
    ctx.single([w = std::move(writes)]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        for (const auto& p : w) cb.setTransform(p.e, p.t);
    });
}

} // namespace rpg
