#include "TerrainAttachSystem.hpp"

#include "Heightmap.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Components.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/System.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/internal/Archetype.hpp>

#include <cmath>
#include <utility>
#include <vector>

namespace rpg {

TerrainAttachSystem::TerrainAttachSystem(const WorldState* worldState,
                                         UserComponentIds* ids,
                                         threadmaxx::Engine* engine) noexcept
    : worldState_(worldState), ids_(ids), engine_(engine) {}

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
    // 2026-05-22 audit (round 3) — fall-to-death emission queue.
    // Populated for entities whose XZ has crossed `±kFallDeathHalfExtent`
    // OR whose Y has gone below `kFallDeathFloorY`. Drained after the
    // chunk walk into `engine_->events<DamageDealt>()`. Only entities
    // with `Health` qualify — the lethal-damage pipeline goes through
    // `DamageSystem` which checks Health presence already.
    struct FallVictim {
        threadmaxx::EntityHandle e;
        threadmaxx::Vec3         pos;
    };
    std::vector<Pending>    writes;
    std::vector<FallVictim> fallVictims;
    PlayerLand landing{{}, {}, false};

    const auto playerH =
        worldState_ ? worldState_->player : threadmaxx::EntityHandle{};
    const auto idsPS = ids_ ? ids_->playerState : threadmaxx::UserComponentId{};

    for (std::size_t i = 0; i < chunkCount; ++i) {
        const auto& chunk = w.archetypeChunk(i);
        if (!chunk.mask.has(threadmaxx::Component::Transform)) continue;
        if (!chunk.mask.has(threadmaxx::Component::Velocity))  continue;
        if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;

        const bool chunkHasHealth =
            chunk.mask.has(threadmaxx::Component::Health);

        const auto n = chunk.entities.size();
        for (std::size_t r = 0; r < n; ++r) {
            const auto& tr = chunk.transforms[r];
            // ---- Fall-to-death detection.
            //
            // 2026-05-22 audit (round 3) — entities that cross past
            // the terrain tile grid lose their physical ground (the
            // heightmap clamps to the boundary so they'd just float)
            // and entities that end up below the world's Y floor are
            // off-screen forever. Both cases route to the death
            // pipeline by emitting a `DamageDealt` of `kFallDeathDamage`
            // — DamageSystem applies it, drops `Health.current` to 0,
            // and emits `EntityDied` like any other kill blow.
            //
            // Only entities with `Health` qualify; the engine doesn't
            // damage entities without a Health column.
            if (chunkHasHealth) {
                const bool offTerrain =
                    std::abs(tr.position.x) > kFallDeathHalfExtent ||
                    std::abs(tr.position.z) > kFallDeathHalfExtent;
                const bool belowFloor = tr.position.y < kFallDeathFloorY;
                if (offTerrain || belowFloor) {
                    const bool alreadyDead =
                        chunk.healths[r].current <= 0.0f;
                    if (!alreadyDead) {
                        fallVictims.push_back({chunk.entities[r], tr.position});
                    }
                    // Skip the normal snap path; the entity is dying
                    // this tick. Keeps it from snapping to the
                    // clamped-boundary terrain Y mid-fall.
                    continue;
                }
            }
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

    // 2026-05-22 audit (round 3) — emit fall-death damage events.
    // Engine-bound; outside the engine (headless tests) we silently
    // drop the events. Emitted on the sim thread inside this `update`
    // call so the next tick's `DamageSystem::preStep` picks them up.
    if (engine_ && !fallVictims.empty()) {
        auto& chDamage = engine_->events<DamageDealt>();
        for (const auto& v : fallVictims) {
            DamageDealt ev;
            ev.attacker = {};                  // environmental kill
            ev.target   = v.e;
            ev.amount   = kFallDeathDamage;
            ev.posX = v.pos.x;
            ev.posY = v.pos.y;
            ev.posZ = v.pos.z;
            chDamage.emit(ev);
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
