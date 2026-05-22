#include "RespawnSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>

#include <algorithm>
#include <cstdio>
#include <utility>

namespace rpg {

void RespawnSystem::preStep(threadmaxx::SystemContext& ctx) {
    drops_.clear();
    disableSwordOnDeath_  = false;
    disablePlayerOnDeath_ = false;
    auto evs = engine_->events<EntityDied>().drainTick();
    const auto playerH =
        worldState_ ? worldState_->player : threadmaxx::EntityHandle{};
    const float dt = static_cast<float>(ctx.dt());
    const float nowSec = static_cast<float>(ctx.tick()) * dt;
    for (const auto& d : evs) {
        // 2026-05-22 audit fix — when the PLAYER is the death
        // event's target, additionally flag the sword for
        // DisabledTag so it stops appearing in the world. Pre-fix
        // the sword stayed Parent-attached to the corpse and the
        // HierarchySystem kept propagating its transform —
        // visible "ghost sword" at the dead player's location.
        // The corpse pickup drop is suppressed for the player
        // (a player-death gold pile would look like a respawn
        // marker, not a game-over indicator).
        //
        // 2026-05-22 audit (round 2) — also stamp the death
        // timestamp on `WorldState::playerDeathTime`. The `update`
        // path below polls this to schedule the respawn.
        if (playerH.valid() && d.entity == playerH) {
            disableSwordOnDeath_  = true;
            disablePlayerOnDeath_ = true;
            if (worldState_) {
                worldState_->playerDeathTime = nowSec;
            }
            std::printf("[rpg_demo] player died at t=%.2fs; respawn in %.1fs\n",
                        static_cast<double>(nowSec),
                        static_cast<double>(kRespawnDelaySeconds));
            continue;
        }

        DropPlan plan;
        plan.corpse = d.entity;
        plan.posX   = d.posX;
        plan.posY   = std::max(d.posY, 0.4f);
        plan.posZ   = d.posZ;
        // §3.5 batch 2: reserve the pickup handle on the sim thread
        // so the spawn command in `update` has a stable handle ahead
        // of commit. Any reservation not consumed is reaped by the
        // engine at step end.
        plan.pickup = engine_->reserveEntityHandle();
        drops_.push_back(plan);
    }
}

void RespawnSystem::update(threadmaxx::SystemContext& ctx) {
    const auto sword =
        worldState_ ? worldState_->sword : threadmaxx::EntityHandle{};
    const auto playerH =
        worldState_ ? worldState_->player : threadmaxx::EntityHandle{};
    const bool disableSword = disableSwordOnDeath_ &&
                              sword.valid() &&
                              ctx.world().alive(sword) &&
                              !ctx.world().hasTag(sword, threadmaxx::Component::DisabledTag);
    // 2026-05-22 audit (round 3) — also flip DisabledTag on the
    // player itself so the renderer hides the corpse. Pre-fix the
    // dead player remained visible (and the camera kept tracking the
    // body) for the full 3-second respawn delay; users reported the
    // game "looked frozen" because the player cube was still there.
    const bool disablePlayer = disablePlayerOnDeath_ &&
                               playerH.valid() &&
                               ctx.world().alive(playerH) &&
                               !ctx.world().hasTag(playerH, threadmaxx::Component::DisabledTag);

    // 2026-05-22 audit (round 2) — player respawn pump. After
    // `kRespawnDelaySeconds` of sim time has elapsed since the
    // recorded death, schedule the respawn: full HP + spawn-point
    // teleport + sword re-enable + PlayerState motion reset.
    const float dt     = static_cast<float>(ctx.dt());
    const float nowSec = static_cast<float>(ctx.tick()) * dt;
    bool doRespawn = false;
    threadmaxx::Vec3 spawnPos{0.0f, 1.0f, 0.0f};
    PlayerState resetState{};
    if (worldState_ && worldState_->playerDeathTime >= 0.0f &&
        playerH.valid() && ctx.world().alive(playerH)) {
        const float elapsed = nowSec - worldState_->playerDeathTime;
        if (elapsed >= kRespawnDelaySeconds) {
            doRespawn = true;
            spawnPos  = worldState_->playerSpawnPos;
            // Preserve persistent fields (kills, pickups, yaw, etc.);
            // reset transient motion / combat fields. Read the live
            // PlayerState so we don't clobber kill / pickup counters.
            if (const auto* livePs = threadmaxx::user::tryGet<PlayerState>(
                    ctx.world(), ids_->playerState, playerH)) {
                resetState = *livePs;
            }
            resetState.verticalVel     = 0.0f;
            resetState.airborne        = 0u;
            resetState.sprinting       = 0u;
            resetState.stamina         = kStaminaMax;
            resetState.swordSwingTimer = 0.0f;
            resetState.blockTimer      = 0.0f;
            worldState_->playerDeathTime = -1.0f;
            std::printf("[rpg_demo] respawning player at (%.2f, %.2f, %.2f)\n",
                        static_cast<double>(spawnPos.x),
                        static_cast<double>(spawnPos.y),
                        static_cast<double>(spawnPos.z));
        }
    }

    if (drops_.empty() && !disableSword && !disablePlayer && !doRespawn) return;

    const auto idsCube   = ids_->cubeRender;
    const auto idsPickup = ids_->pickup;
    const auto idsPS     = ids_->playerState;
    // §3.11 batch 9b.2b — match the floor-pickup meshId so killed-NPC
    // drops draw with the same pyramid mesh (zero = default cube when
    // the renderer callback wasn't wired).
    const std::int32_t pickupMeshId =
        worldState_ ? worldState_->pickupMeshId : 0;
    auto plans = drops_;  // copy by value into the lambda

    ctx.single([plans = std::move(plans), idsCube, idsPickup, pickupMeshId,
                disableSword, disablePlayer, sword, doRespawn, playerH,
                spawnPos, resetState, idsPS]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        if (disableSword) {
            cb.addTag(sword, threadmaxx::Component::DisabledTag);
        }
        if (disablePlayer) {
            cb.addTag(playerH, threadmaxx::Component::DisabledTag);
        }
        if (doRespawn) {
            // Teleport + full heal + reset motion. The orientation
            // is rebuilt next tick by PlayerInputSystem from
            // PlayerState::yawRadians (preserved above), so leaving
            // the quat at identity here is safe.
            threadmaxx::Transform newT{};
            newT.position    = spawnPos;
            newT.scale       = {1.0f, 1.8f, 1.0f};
            newT.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
            cb.setTransform(playerH, newT);
            cb.setHealth(playerH,
                         threadmaxx::Health{kPlayerMaxHP, kPlayerMaxHP});
            cb.setVelocity(playerH,
                           threadmaxx::Velocity{{0, 0, 0}, {0, 0, 0}});
            threadmaxx::addUserComponent(cb, idsPS, playerH, resetState);
            // 2026-05-22 audit (round 3) — un-hide the player +
            // sword. Symmetric with `disablePlayer` / `disableSword`
            // above. If the player is no longer DisabledTag'd (e.g.
            // someone else cleared it) the removeTag is a no-op.
            cb.removeTag(playerH, threadmaxx::Component::DisabledTag);
            if (sword.valid()) {
                cb.removeTag(sword, threadmaxx::Component::DisabledTag);
            }
        }
        for (const auto& p : plans) {
            // §3.11.1 batch D1 — corpse: flip DisabledTag so the
            // renderer skips it and the spatial hash query (next
            // tick) loses interest in it.
            cb.addTag(p.corpse, threadmaxx::Component::DisabledTag);

            // Spawn a gold pickup at the death location into the
            // pre-reserved handle.
            threadmaxx::Bundle b{};
            b.transform.position = {p.posX, p.posY, p.posZ};
            b.transform.scale    = {0.4f, 0.4f, 0.4f};
            b.faction.id         = kFactionNeutral;
            b.boundingVolume.min = {p.posX - 0.4f, p.posY - 0.4f, p.posZ - 0.4f};
            b.boundingVolume.max = {p.posX + 0.4f, p.posY + 0.4f, p.posZ + 0.4f};
            b.initialMask = threadmaxx::ComponentSet{
                threadmaxx::Component::Transform,
                threadmaxx::Component::Faction,
                threadmaxx::Component::BoundingVolume,
            };
            cb.spawnBundle(p.pickup, b);
            {
                CubeRender cr{{1.0f, 0.85f, 0.20f, 1.0f}, 0.7f};
                cr.meshId = pickupMeshId;
                threadmaxx::addUserComponent(cb, idsCube, p.pickup, cr);
            }
            threadmaxx::addUserComponent(cb, idsPickup, p.pickup,
                Pickup{2u});  // killed-NPC drop = 2x value of floor pickup
        }
    });
}

} // namespace rpg
