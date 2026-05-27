#include "HudSystem.hpp"

#include "CameraSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/render/DebugGeometry.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>

#include <algorithm>

namespace tou2d {

namespace {

// Slot colors (0xAABBGGRR): red / blue / green / yellow.
// Per the DebugGeometry comment: A=FF, B=00, G=00, R=FF → 0xFF0000FF = opaque red.
constexpr std::array<std::uint32_t, 4> kSlotColors = {
    0xFF0000FFu,  // P1 red
    0xFFFF0000u,  // P2 blue
    0xFF00FF00u,  // P3 green
    0xFF00FFFFu,  // P4 yellow
};

// Inset from each viewport corner, in world units (the camera's ortho
// half-height is 160; 12 wu ≈ 7.5% of half-height).
constexpr float kInsetWU       = 14.0f;
constexpr float kPipSpacing    = 4.5f;     // horizontal gap between score pips
constexpr float kPipSize       = 4.0f;     // pixel radius
constexpr float kBadgeSize     = 10.0f;    // bigger pixel point for slot badge
constexpr float kHpBarLengthWU = 56.0f;    // full HP = this length
constexpr float kRowVerticalWU = 8.0f;     // vertical gap between badge / pip row / HP bar

/// Pick the (xMul, yMul, growRight) for a slot — each slot gets a
/// distinct corner. xMul ∈ {-1, +1}, yMul ∈ {-1, +1}.
///   slot 0 (P1) → top-left  (-1, +1, growRight=true)
///   slot 1 (P2) → top-right (+1, +1, growRight=false)
///   slot 2 (P3) → bot-left  (-1, -1, growRight=true)
///   slot 3 (P4) → bot-right (+1, -1, growRight=false)
struct CornerAnchor {
    float xMul;
    float yMul;
    bool  growRight;
};
constexpr std::array<CornerAnchor, 4> kCorners = {{
    {-1.0f, +1.0f, true },
    {+1.0f, +1.0f, false},
    {-1.0f, -1.0f, true },
    {+1.0f, -1.0f, false},
}};

} // namespace

HudSystem::HudSystem(UserComponentIds ids, const CameraSystem* camera) noexcept
    : ids_(ids), camera_(camera) {}

void HudSystem::update(threadmaxx::SystemContext& ctx) {
    const auto idsLp   = ids_.localPlayer;
    const auto idsShip = ids_.ship;
    if (!idsLp.valid() || !idsShip.valid()) return;

    // Reset latches each tick.
    for (auto& s : slots_) s = SlotState{};

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& /*cb*/) {
        const auto& view = ctx.worldView();
        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsLp.componentBit()))   continue;
            if (!chunk.mask.has(idsShip.componentBit())) continue;

            const auto lpSpan   = threadmaxx::user::chunkSpan<LocalPlayer>(chunk, idsLp);
            const auto shipSpan = threadmaxx::user::chunkSpan<Ship>(chunk, idsShip);
            const bool disabled =
                chunk.mask.has(threadmaxx::Component::DisabledTag);

            for (std::size_t row = 0, n = lpSpan.size(); row < n; ++row) {
                const std::uint8_t slot = lpSpan[row].slot;
                if (slot >= slots_.size()) continue;
                const Ship& sh = shipSpan[row];
                slots_[slot].present = true;
                slots_[slot].alive   = !disabled;
                slots_[slot].kills   = sh.kills;
                const float maxHp = sh.maxHp > 0.0f ? sh.maxHp : 1.0f;
                slots_[slot].hpFrac = std::clamp(sh.currentHp / maxHp, 0.0f, 1.0f);
            }
        }
    });
}

void HudSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    if (!camera_) return;

    const threadmaxx::Vec3 center = camera_->followCenter();
    const float halfH = camera_->orthoHalfH();
    const float halfW = halfH * camera_->aspect();

    for (std::size_t s = 0; s < slots_.size(); ++s) {
        if (!slots_[s].present) continue;
        const auto&    state = slots_[s];
        const auto&    anchor = kCorners[s];
        const std::uint32_t color =
            state.alive ? kSlotColors[s]
                        : (kSlotColors[s] & 0x00FFFFFFu) | 0x40000000u;  // dim when dead

        // Anchor corner of the viewport in world space.
        const float cornerX = center.x + anchor.xMul * (halfW - kInsetWU);
        const float cornerY = center.y + anchor.yMul * (halfH - kInsetWU);
        const float dir     = anchor.growRight ? +1.0f : -1.0f;

        // ---- Badge point (slot indicator) ------------------------------
        {
            threadmaxx::DebugPoint dp{};
            dp.position  = {cornerX, cornerY, 0.0f};
            dp.colorRGBA = color;
            dp.pixelSize = kBadgeSize;
            b.addDebugPoint(dp);
        }

        // ---- Score pip row (one pip per kill, saturating) --------------
        const std::uint32_t pips =
            std::min<std::uint32_t>(state.kills, kMaxScorePips);
        for (std::uint32_t i = 0; i < pips; ++i) {
            threadmaxx::DebugPoint dp{};
            dp.position  = {
                cornerX + dir * (kBadgeSize * 0.4f + static_cast<float>(i + 1) * kPipSpacing),
                cornerY,
                0.0f,
            };
            dp.colorRGBA = color;
            dp.pixelSize = kPipSize;
            b.addDebugPoint(dp);
        }

        // ---- HP bar (one debug line, length scales with hpFrac) --------
        // Drawn just below the badge row. Background line in dim gray,
        // foreground in slot color (full hp = full kHpBarLengthWU).
        const float barY     = cornerY - kRowVerticalWU * anchor.yMul;
        const float barStart = cornerX;
        const float barEnd   = cornerX + dir * kHpBarLengthWU;
        {
            threadmaxx::DebugLine bg{};
            bg.a         = {barStart, barY, 0.0f};
            bg.b         = {barEnd,   barY, 0.0f};
            bg.colorRGBA = 0x40808080u;  // dim gray track
            b.addDebugLine(bg);
        }
        if (state.hpFrac > 0.0f) {
            const float fillEnd = cornerX + dir * (kHpBarLengthWU * state.hpFrac);
            threadmaxx::DebugLine fg{};
            fg.a         = {barStart, barY, 0.0f};
            fg.b         = {fillEnd,  barY, 0.0f};
            fg.colorRGBA = color;
            b.addDebugLine(fg);
        }
    }
}

} // namespace tou2d
