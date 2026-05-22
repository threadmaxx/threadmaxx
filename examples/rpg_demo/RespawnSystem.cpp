#include "RespawnSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Engine.hpp>
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/UserComponent.hpp>

#include <algorithm>
#include <utility>

namespace rpg {

void RespawnSystem::preStep(threadmaxx::SystemContext&) {
    drops_.clear();
    disableSwordOnDeath_ = false;
    auto evs = engine_->events<EntityDied>().drainTick();
    const auto playerH =
        worldState_ ? worldState_->player : threadmaxx::EntityHandle{};
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
        if (playerH.valid() && d.entity == playerH) {
            disableSwordOnDeath_ = true;
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
    // 2026-05-22 audit fix — also fire the sword-disable on
    // player death even when no NPC drops are pending.
    const auto sword =
        worldState_ ? worldState_->sword : threadmaxx::EntityHandle{};
    const bool disableSword = disableSwordOnDeath_ &&
                              sword.valid() &&
                              ctx.world().alive(sword) &&
                              !ctx.world().hasTag(sword, threadmaxx::Component::DisabledTag);
    if (drops_.empty() && !disableSword) return;

    const auto idsCube   = ids_->cubeRender;
    const auto idsPickup = ids_->pickup;
    // §3.11 batch 9b.2b — match the floor-pickup meshId so killed-NPC
    // drops draw with the same pyramid mesh (zero = default cube when
    // the renderer callback wasn't wired).
    const std::int32_t pickupMeshId =
        worldState_ ? worldState_->pickupMeshId : 0;
    auto plans = drops_;  // copy by value into the lambda

    ctx.single([plans = std::move(plans), idsCube, idsPickup, pickupMeshId,
                disableSword, sword]
               (threadmaxx::Range, threadmaxx::CommandBuffer& cb) {
        if (disableSword) {
            cb.addTag(sword, threadmaxx::Component::DisabledTag);
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
