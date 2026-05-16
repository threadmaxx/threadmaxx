#include "HealthBarSystem.hpp"

#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>

#include <cstdio>

namespace rpg {

void HealthBarSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    if (!world_) return;
    const auto& w = *world_;
    // Iterate every chunk that carries Health + Transform. Skip
    // DisabledTag chunks: dead NPCs shouldn't have a bar.
    const auto chunkCount = w.archetypeChunkCount();
    for (std::size_t i = 0; i < chunkCount; ++i) {
        const auto& chunk = w.archetypeChunk(i);
        if (!chunk.mask.has(threadmaxx::Component::Health)) continue;
        if (!chunk.mask.has(threadmaxx::Component::Transform)) continue;
        if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;
        const auto n = chunk.entities.size();
        for (std::size_t row = 0; row < n; ++row) {
            const auto& hp = chunk.healths[row];
            if (hp.current >= hp.max) continue;  // full HP: no bar
            if (hp.current <= 0.0f)   continue;  // dead but not yet
                                                 // tagged; skip the bar
            const auto& t = chunk.transforms[row];
            char buf[32];
            std::snprintf(buf, sizeof(buf), "HP: %d/%d",
                          static_cast<int>(hp.current),
                          static_cast<int>(hp.max));
            // Floating ~2 units above the entity. Color: red below
            // 33% HP, yellow under 66%, white otherwise.
            const float frac = hp.current / hp.max;
            std::uint32_t color = 0xFFFFFFFFu;
            if      (frac < 0.33f) color = 0xFFFF4040u;
            else if (frac < 0.66f) color = 0xFFFFD040u;
            b.addDebugText(
                threadmaxx::Vec3{t.position.x,
                                 t.position.y + 1.6f,
                                 t.position.z},
                std::string_view(buf),
                color);
        }
    }
}

} // namespace rpg
