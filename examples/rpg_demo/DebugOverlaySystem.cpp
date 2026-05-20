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

    const auto player = worldState_->player;
    if (player.valid() && w.alive(player)) {
        const auto& tr = w.get<threadmaxx::Transform>(player);
        playerPos_ = tr.position;
        const PlayerState* ps =
            threadmaxx::user::tryGet<PlayerState>(w, ids_->playerState, player);
        playerYaw_ = ps ? ps->yawRadians : 0.0f;
        havePlayer_ = true;
    }

    // 2026-05-20 — chunk-walk + nearby-only filter. Pre-fix this
    // iterated the 60k-entity stitched view and emitted AOI rings
    // for every Faction-bearing NPC, producing ~10k × 16 = 160k
    // debug lines per tick under --stress (4ms+ in update, more
    // in buildRenderFrame). The new path walks Faction chunks
    // directly (so 50k pickups + terrain are skipped) and only
    // collects NPCs within `kOverlayRange` of the player (~ a
    // screen-worth). Outside the radius the overlay would be
    // illegible anyway.
    //
    // 2026-05-20 (rev 2) — at 100k NPCs the 30m radius still
    // packed ~50-80k NPCs into the overlay, producing ~1M debug
    // lines per tick which dominated submitFrame on the renderer
    // side (~50 ms CPU pack of the line vertex buffer). Under
    // `--stress` we shrink the radius drastically — the visual
    // detail at 100k NPCs is illegible at 30m anyway, so this is
    // also a UX win.
    const bool stress = worldState_ && worldState_->stressMode;
    const float kOverlayRange   = stress ? 8.0f  : 30.0f;
    const float kOverlayRangeSq = kOverlayRange * kOverlayRange;
    const float px = havePlayer_ ? playerPos_.x : 0.0f;
    const float pz = havePlayer_ ? playerPos_.z : 0.0f;

    const auto npcId = ids_->npcState;
    const auto chunkCount = w.archetypeChunkCount();
    for (std::size_t c = 0; c < chunkCount; ++c) {
        const auto& chunk = w.archetypeChunk(c);
        if (!chunk.mask.has(threadmaxx::Component::Faction)) continue;
        if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;
        if (!chunk.mask.has(npcId.componentBit())) continue;
        auto npcSpan = threadmaxx::user::chunkSpan<NpcState>(chunk, npcId);
        const auto n = chunk.entities.size();
        for (std::size_t r = 0; r < n; ++r) {
            const auto& tr = chunk.transforms[r];
            const float dx = tr.position.x - px;
            const float dz = tr.position.z - pz;
            if (dx * dx + dz * dz > kOverlayRangeSq) continue;
            NpcDebug d;
            d.position = tr.position;
            d.radius   = npcSpan[r].aoiRadius;
            d.color    = (chunk.factions[r].id == kFactionFriendly)
                ? kColorFriendly : kColorHostile;
            npcs_.push_back(d);
        }
    }
}

void DebugOverlaySystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    // AOI circles. 16 segments in normal mode; under --stress drop to 8
    // (twice as cheap, still readable at 8m radius). With the tighter
    // overlay range from update(), peak line count drops from ~1M / tick
    // to ~50k / tick under stress.
    const bool stress = worldState_ && worldState_->stressMode;
    const int  kSeg   = stress ? 8 : 16;
    // 2026-05-20 — precompute the unit-circle samples once instead of
    // recomputing cos/sin per NPC per segment. At 1k NPCs × 16 seg this
    // saved ~0.3 ms; at 100k × 8 seg it saved ~5 ms.
    constexpr int kSegMax = 32;
    float ringCx[kSegMax + 1];
    float ringCz[kSegMax + 1];
    for (int i = 0; i <= kSeg; ++i) {
        const float a = 6.2831853f *
            static_cast<float>(i) / static_cast<float>(kSeg);
        ringCx[i] = std::cos(a);
        ringCz[i] = std::sin(a);
    }
    for (const auto& n : npcs_) {
        for (int i = 0; i < kSeg; ++i) {
            threadmaxx::DebugLine dl;
            dl.a = {n.position.x + ringCx[i] * n.radius,
                    n.position.y + 0.05f,
                    n.position.z + ringCz[i] * n.radius};
            dl.b = {n.position.x + ringCx[i + 1] * n.radius,
                    n.position.y + 0.05f,
                    n.position.z + ringCz[i + 1] * n.radius};
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
