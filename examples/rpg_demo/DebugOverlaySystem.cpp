#include "DebugOverlaySystem.hpp"

#include <threadmaxx/Components.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>

#include <cmath>

namespace rpg {

namespace {

constexpr std::uint32_t kColorFriendly = 0xFF00FF40u;   // green
constexpr std::uint32_t kColorHostile  = 0xFF0040FFu;   // red
constexpr std::uint32_t kColorAim      = 0xFFFFFF00u;   // yellow
constexpr std::uint32_t kColorPickup   = 0xFF00D0FFu;   // gold

} // namespace

void DebugOverlaySystem::update(threadmaxx::SystemContext& ctx) {
    npcs_.clear();
    havePlayer_ = false;

    const auto& w = ctx.world();
    const auto entities = w.entities();
    const auto masks = w.componentMasks();
    const auto transforms = w.transforms();

    const auto player = worldState_->player;
    if (player.valid() && w.alive(player)) {
        const auto& tr = w.get<threadmaxx::Transform>(player);
        playerPos_ = tr.position;
        const PlayerState* ps =
            threadmaxx::user::tryGet<PlayerState>(w, ids_->playerState, player);
        playerYaw_ = ps ? ps->yawRadians : 0.0f;
        havePlayer_ = true;
    }

    for (std::size_t i = 0; i < entities.size(); ++i) {
        if (!masks[i].has(threadmaxx::Component::Faction))    continue;
        if (masks[i].has(threadmaxx::Component::DisabledTag)) continue;
        const auto e = entities[i];
        const NpcState* st = threadmaxx::user::tryGet<NpcState>(w, ids_->npcState, e);
        if (!st) continue;
        const auto& fac = w.get<threadmaxx::Faction>(e);
        NpcDebug d;
        d.position = transforms[i].position;
        d.radius   = st->aoiRadius;
        d.color    = (fac.id == kFactionFriendly) ? kColorFriendly : kColorHostile;
        npcs_.push_back(d);
    }
}

void DebugOverlaySystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    // AOI circles, 16 segments each.
    constexpr int kSeg = 16;
    for (const auto& n : npcs_) {
        for (int i = 0; i < kSeg; ++i) {
            const float a0 = 6.2831853f *  static_cast<float>(i)      / static_cast<float>(kSeg);
            const float a1 = 6.2831853f *  static_cast<float>(i + 1)  / static_cast<float>(kSeg);
            threadmaxx::DebugLine dl;
            dl.a = {n.position.x + std::cos(a0) * n.radius,
                    n.position.y + 0.05f,
                    n.position.z + std::sin(a0) * n.radius};
            dl.b = {n.position.x + std::cos(a1) * n.radius,
                    n.position.y + 0.05f,
                    n.position.z + std::sin(a1) * n.radius};
            dl.colorRGBA = n.color;
            b.addDebugLine(dl);
        }
        // Highlight the NPC as a small upright point.
        threadmaxx::DebugPoint pt;
        pt.position = {n.position.x, n.position.y + 1.6f, n.position.z};
        pt.colorRGBA = n.color;
        pt.pixelSize = 6.0f;
        b.addDebugPoint(pt);
    }

    if (havePlayer_) {
        // Player aim line — 4 units in the facing direction.
        const float cy = std::cos(playerYaw_);
        const float sy = std::sin(playerYaw_);
        threadmaxx::DebugLine aim;
        aim.a = {playerPos_.x, playerPos_.y + 1.2f, playerPos_.z};
        aim.b = {playerPos_.x - sy * 4.0f,
                 playerPos_.y + 1.2f,
                 playerPos_.z - cy * 4.0f};
        aim.colorRGBA = kColorAim;
        b.addDebugLine(aim);

        // Tiny point above the player.
        threadmaxx::DebugPoint head;
        head.position = {playerPos_.x, playerPos_.y + 2.4f, playerPos_.z};
        head.colorRGBA = kColorPickup;
        head.pixelSize = 8.0f;
        b.addDebugPoint(head);
    }
}

} // namespace rpg
