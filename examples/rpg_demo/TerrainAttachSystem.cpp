#include "TerrainAttachSystem.hpp"

#include "Heightmap.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/System.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/internal/Archetype.hpp>

#include <utility>
#include <vector>

namespace rpg {

TerrainAttachSystem::TerrainAttachSystem(const WorldState* worldState,
                                         UserComponentIds* ids) noexcept
    : worldState_(worldState), ids_(ids) {}

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
    //
    // 2026-05-22 audit refactor — the player is identified by a
    // UserComponent<PlayerState>. When the player is airborne
    // (mid-jump) we skip the Y snap and let PlayerInputSystem own
    // the Y integration. When the falling player crosses below
    // the ground we re-engage: snap to the ground AND reset
    // `verticalVel = 0` + `airborne = false` so the input system
    // can issue another jump.

    struct Pending {
        threadmaxx::EntityHandle e;
        threadmaxx::Transform    t;
    };
    struct PlayerLand {
        threadmaxx::EntityHandle e;
        PlayerState              state;
        bool                     write;
    };
    std::vector<Pending> writes;
    PlayerLand landing{{}, {}, false};

    const auto playerH =
        worldState_ ? worldState_->player : threadmaxx::EntityHandle{};
    const auto idsPS = ids_ ? ids_->playerState : threadmaxx::UserComponentId{};

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
            const bool isPlayer = playerH.valid() &&
                                  chunk.entities[r] == playerH;
            if (isPlayer) {
                const PlayerState* ps =
                    threadmaxx::user::tryGet<PlayerState>(w, idsPS, playerH);
                if (ps && ps->airborne != 0u) {
                    // Mid-jump path: don't snap. Player has crossed
                    // back to/below the ground when `position.y -
                    // groundY` <= kGroundedSlack AND the jump's
                    // verticalVel is non-positive (descending).
                    const float gap = tr.position.y - groundY;
                    if (gap <= kGroundedSlack && ps->verticalVel <= 0.0f) {
                        // Snap + land.
                        threadmaxx::Transform out = tr;
                        out.position.y = groundY;
                        writes.push_back({chunk.entities[r], out});
                        landing.e     = playerH;
                        landing.state = *ps;
                        landing.state.verticalVel = 0.0f;
                        landing.state.airborne    = 0u;
                        landing.write = true;
                    }
                    continue;
                }
            }
            if (tr.position.y == groundY) continue;
            threadmaxx::Transform out = tr;
            out.position.y = groundY;
            writes.push_back({chunk.entities[r], out});
        }
    }

    if (writes.empty() && !landing.write) return;
    ctx.single([writes = std::move(writes), landing, idsPS]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        for (const auto& p : writes) cb.setTransform(p.e, p.t);
        if (landing.write) {
            threadmaxx::addUserComponent(cb, idsPS, landing.e, landing.state);
        }
    });
}

} // namespace rpg
