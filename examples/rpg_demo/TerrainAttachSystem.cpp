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
    // 2026-05-22 audit (round 6) — captured at the moment the
    // player crosses back to the ground; emitted as a `JumpLanded`
    // event after the chunk walk. Local scope so this stays a
    // per-tick value, not system state.
    threadmaxx::Vec3 landingPos{0.0f, 0.0f, 0.0f};
    float            impactSpeed = 0.0f;

    const auto playerH =
        worldState_ ? worldState_->player : threadmaxx::EntityHandle{};
    const auto idsPS = ids_ ? ids_->playerState : threadmaxx::UserComponentId{};

    const float dt = static_cast<float>(ctx.dt());

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
            const bool isPlayer = playerH.valid() &&
                                  chunk.entities[r] == playerH;
            const bool offTerrain =
                std::abs(tr.position.x) > kFallDeathHalfExtent ||
                std::abs(tr.position.z) > kFallDeathHalfExtent;
            const bool belowFloor = tr.position.y < kFallDeathFloorY;

            // ---- Lethal: below the world's Y floor.
            //
            // 2026-05-22 audit (round 4) — only the Y-floor crossing
            // is instantly lethal now. Walking past the XZ edge used
            // to be an instant kill (round 3) but that read as
            // "invisible wall of death" rather than "you fell off
            // the edge." The fall mechanic below replaces it: past
            // the edge entities lose ground contact and start
            // dropping; lethal damage fires when they cross the
            // floor.
            if (chunkHasHealth && belowFloor) {
                const bool alreadyDead =
                    chunk.healths[r].current <= 0.0f;
                if (!alreadyDead) {
                    fallVictims.push_back({chunk.entities[r], tr.position});
                }
                continue;
            }

            // ---- Off-terrain: skip the ground snap, let the entity
            //      fall toward the Y floor.
            //
            // For the PLAYER we don't write Y here — PlayerInputSystem
            // owns the player's vertical integration via the airborne
            // path. We just promote them to airborne (with
            // verticalVel = 0) so the next tick's PlayerInputSystem
            // starts pulling them down. If they're already airborne
            // (mid-jump that walked off the edge), do nothing — their
            // ongoing integration is the fall.
            //
            // For NON-PLAYER entities (NPCs, particles) we don't have
            // a per-entity Y velocity to integrate, so we write a
            // constant-rate Y decrement directly into the Transform.
            // This is precise enough for a visible fall before the
            // entity passes the Y floor.
            if (offTerrain) {
                if (isPlayer) {
                    const PlayerState* ps =
                        threadmaxx::user::tryGet<PlayerState>(w, idsPS, playerH);
                    if (ps && ps->airborne == 0u) {
                        landing.e                 = playerH;
                        landing.state             = *ps;
                        landing.state.verticalVel = 0.0f;
                        landing.state.airborne    = 1u;
                        landing.write             = true;
                    }
                    // Player Y owned by PlayerInputSystem from here on.
                    continue;
                }
                threadmaxx::Transform out = tr;
                out.position.y = tr.position.y + kOffTerrainFallSpeed * dt;
                writes.push_back({chunk.entities[r], out});
                continue;
            }

            const float halfY = tr.scale.y * 0.5f;

            // 2026-05-22 audit (round 9, voxel pivot) — step-up
            // rejection. If the new XZ falls inside a cell whose
            // quantized height exceeds the entity's previous-tick
            // ground height by more than `kStepUpMax` block units,
            // revert XZ to the prev safe pos so the entity stops at
            // the wall instead of teleporting on top. Airborne
            // players bypass: jumping over walls is the intended
            // escape hatch. Particles + dead-XZ entities (the
            // off-terrain branch above) never reach here.
            //
            // The prev-pos cache is keyed by `EntityHandle::index`;
            // the generation check filters stale entries from
            // recycled slots.
            const auto handle = chunk.entities[r];
            const auto idx    = static_cast<std::size_t>(handle.index);
            if (idx >= prevPos_.size()) prevPos_.resize(idx + 1);
            PrevSlot& prev = prevPos_[idx];
            const bool prevValid =
                prev.valid && prev.generation == handle.generation;

            const PlayerState* ps =
                isPlayer ? threadmaxx::user::tryGet<PlayerState>(
                              w, idsPS, playerH) : nullptr;
            const bool playerAirborne = (ps && ps->airborne != 0u);

            float finalX = tr.position.x;
            float finalZ = tr.position.z;
            const float candidateGroundY =
                hmap->heightAt(finalX, finalZ);
            const float prevGroundY = prevValid
                ? hmap->heightAt(prev.pos.x, prev.pos.z)
                : candidateGroundY;
            bool reverted = false;
            if (prevValid && !playerAirborne &&
                candidateGroundY - prevGroundY > kStepUpMax) {
                finalX   = prev.pos.x;
                finalZ   = prev.pos.z;
                reverted = true;
            }

            // Recompute the final ground based on the (possibly
            // reverted) XZ. With the voxel heightmap this is either
            // `candidateGroundY` (no revert) or `prevGroundY`
            // (reverted).
            const float groundOnly = reverted ? prevGroundY : candidateGroundY;
            const float groundY    = groundOnly + halfY;

            if (isPlayer && playerAirborne) {
                // Mid-jump path: don't snap. Player has crossed
                // back to/below the ground when `position.y -
                // groundY` <= kGroundedSlack AND the jump's
                // verticalVel is non-positive (descending).
                const float gap = tr.position.y - groundY;
                if (gap <= kGroundedSlack && ps->verticalVel <= 0.0f) {
                    // Snap + land. Capture the impact velocity
                    // BEFORE we zero it so the JumpLanded event
                    // can carry it.
                    impactSpeed = -ps->verticalVel;
                    landingPos  = {finalX, groundY, finalZ};
                    threadmaxx::Transform out = tr;
                    out.position.x = finalX;
                    out.position.z = finalZ;
                    out.position.y = groundY;
                    writes.push_back({handle, out});
                    landing.e     = playerH;
                    landing.state = *ps;
                    landing.state.verticalVel = 0.0f;
                    landing.state.airborne    = 0u;
                    landing.write = true;
                    prev.pos        = {finalX, groundOnly, finalZ};
                    prev.generation = handle.generation;
                    prev.valid      = true;
                }
                continue;
            }

            if (reverted || tr.position.y != groundY ||
                tr.position.x != finalX || tr.position.z != finalZ) {
                threadmaxx::Transform out = tr;
                out.position.x = finalX;
                out.position.z = finalZ;
                out.position.y = groundY;
                writes.push_back({handle, out});
            }

            // Note: on revert we deliberately DO NOT zero linear
            // velocity. The entity "leans into the wall" — its XZ
            // is restored each tick, but its velocity stays set by
            // its owning system (PlayerInputSystem reads keys every
            // tick; NPCBrainSystem re-issues every ~1.5s). Zeroing
            // velocity here was tested and broken animation tests
            // because the cosmetic Y-bob is gated on linear speed
            // > 0 (the bob "footstep-rise" needs the speed signal).
            // The 1-tick "into the wall" overshoot is sub-cm at
            // typical NPC speeds and reverted before any visible
            // frame, so the visual is identical to a hard stop.

            prev.pos        = {finalX, groundOnly, finalZ};
            prev.generation = handle.generation;
            prev.valid      = true;
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

    // 2026-05-22 audit (round 6) — emit JumpLanded for the player
    // whenever a mid-jump descent crossed back to the ground this
    // tick. ParticleEmitterSystem drains the channel and bursts a
    // dust puff at the landing position. Headless / engine-null
    // path silently drops it.
    if (engine_ && landing.write && landing.e.valid() &&
        landing.e == playerH) {
        JumpLanded ev;
        ev.entity      = playerH;
        ev.posX        = landingPos.x;
        ev.posY        = landingPos.y;
        ev.posZ        = landingPos.z;
        ev.impactSpeed = impactSpeed;
        engine_->events<JumpLanded>().emit(ev);
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
