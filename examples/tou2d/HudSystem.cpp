#include "HudSystem.hpp"

#include "CameraSystem.hpp"

#include <threadmaxx/CommandBuffer.hpp>
#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/render/DebugGeometry.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>

#include <algorithm>
#include <cmath>

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

// Base WU constants (at hudScale = 100). M6.7: every call site reads
// these through `scaled(s, sc)` so the accessibility hudScale slider
// (50..200%) rescales the entire HUD without recompiling.
constexpr float kBaseInsetWU       = 14.0f;
constexpr float kBasePipSpacing    = 4.5f;     // horizontal gap between score pips
constexpr float kBasePipSize       = 4.0f;     // pixel radius
constexpr float kBaseBadgeSize     = 10.0f;    // bigger pixel point for slot badge
constexpr float kBaseHpBarLengthWU = 56.0f;    // full HP = this length
constexpr float kBaseRowVerticalWU = 8.0f;     // vertical gap between badge / pip row / HP bar

// M4.2 — ammo strip + winner banner constants.
constexpr float kBaseAmmoPipSize     = 2.6f;   // smaller than score pips
constexpr float kBaseAmmoPipSpacing  = 3.4f;   // tighter than score pips
constexpr float kBaseAmmoRowGapWU    = 6.0f;   // gap below HP bar
constexpr float kAmmoTrackAlpha8     = 56.0f;  // dim placeholder for empty mag slots
constexpr float kBaseWeaponGlyphWidthWU  = 7.0f;
constexpr float kBaseWeaponGlyphHeightWU = 3.0f;
constexpr std::uint32_t kBannerWhite      = 0xFFFFFFFFu;
constexpr float kBaseBannerHalfWidthWU    = 90.0f;
constexpr float kBaseBannerHalfHeightWU   = 22.0f;
constexpr float kBaseBannerBadgeSize      = 14.0f;
constexpr float kBaseBannerKillsPipSize   = 5.0f;

// M6.7 — HP bar thickening: the bar is drawn as a stack of `kHpBarLines`
// parallel DebugLine segments offset by `kHpBarLineSpacing` along Y so
// it visually reads as a thick bar without needing a textured quad.
constexpr int   kHpBarLines       = 3;
constexpr float kHpBarLineSpacing = 0.55f;  // wu between stacked lines
// M6.7 — low-HP red overlay. The pulse fires per HUD tick; alpha runs
// between [kPulseMinAlpha, kPulseMaxAlpha] on a `kPulsePeriod`-tick sine.
constexpr std::uint32_t kLowHpRed  = 0x000000FFu;  // RGB only; alpha filled in
constexpr float kPulseMinAlpha = 96.0f;
constexpr float kPulseMaxAlpha = 220.0f;
constexpr float kPulsePeriod   = 20.0f;
// M6.7 — top-of-viewport low-HP warning marker. Anchored a small
// fraction of `halfH` below the top edge so it sits inside the cull rect.
constexpr float kWarningTopInsetFrac = 0.08f;
constexpr float kBaseWarningSize     = 8.0f;
constexpr float kBigWarningMultiplier = 2.0f;

// M6.7 — single-source-of-truth for hudScale: a no-op at 100%, lerps to
// 50..200% via integer percent. Each base-WU constant funnels through
// this so the entire layout scales uniformly without touching call
// sites individually.
inline float hudScaleFactor(const AccessibilitySettings& a) noexcept {
    // Clamp defensively (0 / >255 yield degenerate layouts).
    const std::uint8_t pct = a.hudScale == 0 ? std::uint8_t{100} : a.hudScale;
    return static_cast<float>(pct) / 100.0f;
}

} // namespace

HudSystem::HudSystem(UserComponentIds ids, const CameraSystem* camera) noexcept
    : ids_(ids), camera_(camera) {}

void HudSystem::pushSlotStateForTest(std::uint8_t slot, bool present, bool alive,
                                     float hpFrac, std::uint32_t kills) noexcept {
    if (slot >= slots_.size()) return;
    auto& s = slots_[slot];
    s.present = present;
    s.alive   = alive;
    s.hpFrac  = std::clamp(hpFrac, 0.0f, 1.0f);
    s.kills   = kills;
}

void HudSystem::advancePulseForTest(std::uint32_t ticks) noexcept {
    pulseTick_ += ticks;
}

void HudSystem::update(threadmaxx::SystemContext& ctx) {
    // M6.7 — cosmetic pulse tick for the low-HP overlay. Advances every
    // step regardless of accessibility/alive state — render-side reads
    // `pulseTick_` to drive the sine.
    ++pulseTick_;

    const auto idsLp     = ids_.localPlayer;
    const auto idsShip   = ids_.ship;
    const auto idsLd     = ids_.loadout;
    const auto idsPickup = ids_.pickup;
    if (!idsLp.valid() || !idsShip.valid()) return;

    // Reset latches each tick.
    for (auto& s : slots_) s = SlotState{};
    // N2 — clear kit latch; capacity is preserved across ticks.
    kitPositionsXY_.clear();

    ctx.single([&](threadmaxx::Range /*r*/, threadmaxx::CommandBuffer& /*cb*/) {
        const auto& view = ctx.worldView();

        // N2 — pre-pass for active RepairKit positions. Runs over the
        // same `view.chunks()` snapshot but is gated on idsPickup so it
        // skips ship chunks entirely. Active kits = state==0 AND no
        // DisabledTag (respawning kits carry DisabledTag set by
        // RepairKitSystem).
        if (idsPickup.valid()) {
            for (const auto* chunkPtr : view.chunks()) {
                if (!chunkPtr) continue;
                const auto& chunk = *chunkPtr;
                if (!chunk.mask.has(idsPickup.componentBit())) continue;
                if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;
                if (!chunk.mask.has(threadmaxx::Component::Transform)) continue;

                const auto pickupSpan =
                    threadmaxx::user::chunkSpan<Pickup>(chunk, idsPickup);
                const auto transformSpan =
                    threadmaxx::detail::getChunkSpan<threadmaxx::Transform>(chunk);

                for (std::size_t row = 0, n = pickupSpan.size(); row < n; ++row) {
                    if (kitPositionsXY_.size() >= kMaxKitGlyphs) break;
                    const Pickup& pk = pickupSpan[row];
                    if (pk.state != 0) continue;
                    const auto& tr = transformSpan[row];
                    kitPositionsXY_.emplace_back(tr.position.x, tr.position.y);
                }
                if (kitPositionsXY_.size() >= kMaxKitGlyphs) break;
            }
        }

        for (const auto* chunkPtr : view.chunks()) {
            if (!chunkPtr) continue;
            const auto& chunk = *chunkPtr;
            if (!chunk.mask.has(idsLp.componentBit()))   continue;
            if (!chunk.mask.has(idsShip.componentBit())) continue;

            const auto lpSpan   = threadmaxx::user::chunkSpan<LocalPlayer>(chunk, idsLp);
            const auto shipSpan = threadmaxx::user::chunkSpan<Ship>(chunk, idsShip);
            const bool hasLd = idsLd.valid() && chunk.mask.has(idsLd.componentBit());
            const auto ldSpan = hasLd
                ? threadmaxx::user::chunkSpan<WeaponLoadout>(chunk, idsLd)
                : std::span<const WeaponLoadout>{};
            const bool disabled =
                chunk.mask.has(threadmaxx::Component::DisabledTag);

            for (std::size_t row = 0, n = lpSpan.size(); row < n; ++row) {
                const std::uint8_t slot = lpSpan[row].slot;
                if (slot >= slots_.size()) continue;
                const Ship& sh = shipSpan[row];
                slots_[slot].present       = true;
                slots_[slot].alive         = !disabled;
                slots_[slot].permanentDead =
                    disabled && sh.respawnIn == kPermanentDeathSentinel;
                slots_[slot].kills   = sh.kills;
                const float maxHp = sh.maxHp > 0.0f ? sh.maxHp : 1.0f;
                slots_[slot].hpFrac = std::clamp(sh.currentHp / maxHp, 0.0f, 1.0f);
                if (hasLd) {
                    const WeaponLoadout& ld = ldSpan[row];
                    slots_[slot].dumbfireAmmo   = ld.dumbfireAmmo;
                    slots_[slot].dumbfireReload = ld.dumbfireReloadIn;
                    slots_[slot].specialAmmo    = ld.specialAmmo;
                    slots_[slot].specialReload  = ld.specialReloadIn;
                    slots_[slot].specialKind    = ld.specialKind;
                }
            }
        }
    });
}

void HudSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    if (!camera_) return;

    // M5.7 — read the layout-effective half-height. With 3-4 humans
    // this is half the design value, so the HUD's TL-corner anchor
    // (which adds ±halfH / ±halfW offsets) lands at the smaller
    // viewport's actual corner rather than off-screen.
    const float halfH  = camera_->effectiveOrthoHalfH();
    const float halfW  = halfH * camera_->viewportAspect();
    const std::uint8_t numHumans = camera_->numHumans();

    // M6.7 — accessibility-driven scale. Every WU constant in this
    // function reads through `sc` so the entire HUD lives at one of
    // 50/75/100/125/150/175/200% uniformly. PixelSize follows the same
    // factor — a 75% HUD is both narrower and made of smaller dots.
    const float sc = hudScaleFactor(access_);
    const float kInsetWU       = kBaseInsetWU       * sc;
    const float kPipSpacing    = kBasePipSpacing    * sc;
    const float kPipSize       = kBasePipSize       * sc;
    const float kBadgeSize     = kBaseBadgeSize     * sc;
    const float kHpBarLengthWU = kBaseHpBarLengthWU * sc;
    const float kRowVerticalWU = kBaseRowVerticalWU * sc;
    const float kAmmoPipSize    = kBaseAmmoPipSize    * sc;
    const float kAmmoPipSpacing = kBaseAmmoPipSpacing * sc;
    const float kAmmoRowGapWU   = kBaseAmmoRowGapWU   * sc;
    const float kWeaponGlyphWidthWU  = kBaseWeaponGlyphWidthWU  * sc;
    const float kWeaponGlyphHeightWU = kBaseWeaponGlyphHeightWU * sc;
    const float barLineSpacing       = kHpBarLineSpacing        * sc;

    // M5.1 — per-viewport HUD: each human renders only their own
    // badge/HP/ammo, anchored to the TOP-LEFT corner of THAT human's
    // viewport (in world space, relative to their own camera's follow
    // center). Bots are never iterated.
    //
    // M7.2 — per-slot HUD primitives carry `cameraMask = (1u << s)`,
    // so the Vulkan renderer draws them ONLY in slot `s`'s camera
    // pass. CameraSystem registers cameras in slot order
    // (RenderFrame::cameras[i] == slot i's camera), so the bit→camera
    // mapping is direct. Pre-M7.2 the HUD lived at default
    // (all-ones) and would appear in every camera whose frustum
    // contained the anchor point — close-combat overlap was the
    // visible symptom this batch closes.
    for (std::uint8_t s = 0; s < numHumans; ++s) {
        if (!slots_[s].present) continue;
        const auto&    state = slots_[s];
        const std::uint32_t color =
            state.alive ? kSlotColors[s]
                        : (kSlotColors[s] & 0x00FFFFFFu) | 0x40000000u;  // dim when dead
        const std::uint32_t slotCameraMask = (1u << s);

        const threadmaxx::Vec3 center = camera_->followCenter(s);

        // Pin every human's HUD to the TOP-LEFT of their own viewport.
        // Each viewport is centered on its ship; (-halfW + inset, +halfH - inset)
        // is the top-left corner in world units; pips grow to the right.
        const float cornerX = center.x - (halfW - kInsetWU);
        const float cornerY = center.y + (halfH - kInsetWU);
        const float dir     = +1.0f;

        // ---- Badge point (slot indicator) ------------------------------
        {
            threadmaxx::DebugPoint dp{};
            dp.position   = {cornerX, cornerY, 0.0f};
            dp.colorRGBA  = color;
            dp.pixelSize  = kBadgeSize;
            dp.cameraMask = slotCameraMask;
            b.addDebugPoint(dp);
        }

        // ---- Score pip row (one pip per kill, saturating) --------------
        const std::uint32_t pips =
            std::min<std::uint32_t>(state.kills, kMaxScorePips);
        for (std::uint32_t i = 0; i < pips; ++i) {
            threadmaxx::DebugPoint dp{};
            dp.position   = {
                cornerX + dir * (kBadgeSize * 0.4f + static_cast<float>(i + 1) * kPipSpacing),
                cornerY,
                0.0f,
            };
            dp.colorRGBA  = color;
            dp.pixelSize  = kPipSize;
            dp.cameraMask = slotCameraMask;
            b.addDebugPoint(dp);
        }

        // ---- HP bar (one debug line, length scales with hpFrac) --------
        // Drawn just below the badge row. Background line in dim gray,
        // foreground in slot color (full hp = full kHpBarLengthWU).
        const float barY     = cornerY - kRowVerticalWU;
        const float barStart = cornerX;
        const float barEnd   = cornerX + dir * kHpBarLengthWU;

        // M4.3 — LSS permanent-dead: replace the HP bar + ammo strips
        // with a slot-colored "X" spanning the HP-bar footprint so the
        // spectator can see at a glance which slot is out.
        if (state.permanentDead) {
            const float xHalfH = kRowVerticalWU * 1.5f;
            const std::uint32_t xColor = (kSlotColors[s] & 0x00FFFFFFu) | 0xC0000000u;
            threadmaxx::DebugLine d1{};
            d1.a          = {barStart, barY + xHalfH, 0.0f};
            d1.b          = {barEnd,   barY - xHalfH, 0.0f};
            d1.colorRGBA  = xColor;
            d1.cameraMask = slotCameraMask;
            b.addDebugLine(d1);
            threadmaxx::DebugLine d2{};
            d2.a          = {barStart, barY - xHalfH, 0.0f};
            d2.b          = {barEnd,   barY + xHalfH, 0.0f};
            d2.colorRGBA  = xColor;
            d2.cameraMask = slotCameraMask;
            b.addDebugLine(d2);
            continue;   // skip HP bar + ammo strips for this slot
        }

        // M6.7 — thickened HP bar. Stacks `kHpBarLines` parallel
        // DebugLine segments along Y; the outer pair are dim-white
        // "outline" strokes that frame the colored bar so it pops
        // against arbitrary terrain colors.
        const auto emitStackedLine = [&](float ax, float bx, float yMid,
                                         std::uint32_t col) {
            for (int i = 0; i < kHpBarLines; ++i) {
                const float dy =
                    (static_cast<float>(i) -
                     0.5f * static_cast<float>(kHpBarLines - 1)) *
                    barLineSpacing;
                threadmaxx::DebugLine ln{};
                ln.a          = {ax, yMid + dy, 0.0f};
                ln.b          = {bx, yMid + dy, 0.0f};
                ln.colorRGBA  = col;
                ln.cameraMask = slotCameraMask;
                b.addDebugLine(ln);
            }
        };
        // Background track (dim gray, full bar length).
        emitStackedLine(barStart, barEnd, barY, 0x40808080u);
        // Outline strokes above + below the stack — frame the bar.
        const float outlineDy =
            barLineSpacing * (0.5f * static_cast<float>(kHpBarLines - 1) + 1.0f);
        {
            threadmaxx::DebugLine top{};
            top.a          = {barStart, barY + outlineDy, 0.0f};
            top.b          = {barEnd,   barY + outlineDy, 0.0f};
            top.colorRGBA  = 0x60FFFFFFu;
            top.cameraMask = slotCameraMask;
            b.addDebugLine(top);
            threadmaxx::DebugLine bot{};
            bot.a          = {barStart, barY - outlineDy, 0.0f};
            bot.b          = {barEnd,   barY - outlineDy, 0.0f};
            bot.colorRGBA  = 0x60FFFFFFu;
            bot.cameraMask = slotCameraMask;
            b.addDebugLine(bot);
        }
        if (state.hpFrac > 0.0f) {
            const float fillEnd = cornerX + dir * (kHpBarLengthWU * state.hpFrac);
            // M6.7 — low-HP red pulse. Below the threshold, the colored
            // fill is replaced by a pulsing red so the player has a
            // peripheral cue independent of slot color (P1 red could
            // otherwise mask its own low-HP signal).
            std::uint32_t fillColor = color;
            if (state.hpFrac <= kLowHpFracThreshold) {
                const float phase =
                    static_cast<float>(pulseTick_) * (6.2831853f / kPulsePeriod);
                // sin -> [-1, 1] -> remap to [kPulseMinAlpha, kPulseMaxAlpha].
                const float a01 = 0.5f * (std::sin(phase) + 1.0f);
                const float a8f =
                    kPulseMinAlpha + a01 * (kPulseMaxAlpha - kPulseMinAlpha);
                const std::uint32_t a8 = static_cast<std::uint32_t>(
                    std::clamp(a8f, 0.0f, 255.0f));
                fillColor = kLowHpRed | (a8 << 24);
            }
            emitStackedLine(barStart, fillEnd, barY, fillColor);
        }

        // M6.7 — top-of-viewport warning marker. Mirrors the low-HP
        // condition; doubles in size when `bigWarnings` is on. Anchored
        // a small fraction of `halfH` below the top edge so it sits in
        // the cull rect regardless of HUD scale.
        if (state.hpFrac > 0.0f && state.hpFrac <= kLowHpFracThreshold) {
            const float markerPxBase  = kBaseWarningSize * sc;
            const float markerPx      =
                access_.bigWarnings ? markerPxBase * kBigWarningMultiplier
                                    : markerPxBase;
            threadmaxx::DebugPoint dp{};
            dp.position   = {
                center.x,
                center.y + halfH * (1.0f - kWarningTopInsetFrac),
                0.0f,
            };
            // Solid red, same fixed color as the low-HP pulse track —
            // skip the pulse to keep the marker as a steady warning.
            dp.colorRGBA  = 0xE00000FFu;
            dp.pixelSize  = markerPx;
            dp.cameraMask = slotCameraMask;
            b.addDebugPoint(dp);
        }

        // ---- Weapon glyph + ammo strip --------------------------------
        // Each row leads with a small weapon-identifier glyph (M4.4):
        //   * Dumbfire — one short horizontal bar (a single bullet).
        //   * Spread   — three short fanning ticks (the 3-pellet
        //                  cone the weapon actually fires).
        // The glyph occupies kWeaponGlyphWidthWU of horizontal space;
        // ammo pips are shifted that far in the row direction so the
        // glyph + pips read as a single banner per weapon.
        //
        // 0 = Dumbfire, 1 = Spread. Picked by `weaponKind` so the
        // glyph encoding lives next to its visual definition.
        const auto drawWeaponGlyph = [&](float rowY,
                                         int weaponKind,
                                         std::uint32_t glyphColor) {
            // Glyph origin sits just inboard of the corner; the glyph
            // grows in `dir` toward where the pips will start. Half-
            // height controlled by kWeaponGlyphHeightWU.
            const float gxStart = cornerX + dir * 0.5f;
            const float gxEnd   = cornerX + dir * (kWeaponGlyphWidthWU - 0.5f);
            if (weaponKind == 0) {
                // Dumbfire — single forward bar.
                threadmaxx::DebugLine ln{};
                ln.a          = {gxStart, rowY, 0.0f};
                ln.b          = {gxEnd,   rowY, 0.0f};
                ln.colorRGBA  = glyphColor;
                ln.cameraMask = slotCameraMask;
                b.addDebugLine(ln);
            } else {
                // Spread — three ticks fanning ±kSpreadAngle around
                // forward. Glyph "origin" is the tail (gxStart); the
                // three tips spread out at gxEnd with the outer two
                // offset vertically by kWeaponGlyphHeightWU.
                const float tipMid = gxEnd;
                const float tipUp  = gxEnd - dir * 1.0f;  // slight pull-back so the
                const float tipDn  = gxEnd - dir * 1.0f;  // outer ticks read distinct
                threadmaxx::DebugLine mid{};
                mid.a          = {gxStart, rowY, 0.0f};
                mid.b          = {tipMid,  rowY, 0.0f};
                mid.colorRGBA  = glyphColor;
                mid.cameraMask = slotCameraMask;
                b.addDebugLine(mid);
                threadmaxx::DebugLine up{};
                up.a          = {gxStart, rowY, 0.0f};
                up.b          = {tipUp,   rowY + kWeaponGlyphHeightWU, 0.0f};
                up.colorRGBA  = glyphColor;
                up.cameraMask = slotCameraMask;
                b.addDebugLine(up);
                threadmaxx::DebugLine dn{};
                dn.a          = {gxStart, rowY, 0.0f};
                dn.b          = {tipDn,   rowY - kWeaponGlyphHeightWU, 0.0f};
                dn.colorRGBA  = glyphColor;
                dn.cameraMask = slotCameraMask;
                b.addDebugLine(dn);
            }
        };

        // ---- Ammo strip (one tight pip per remaining round) -----------
        // Two rows: dumbfire (closer to HP bar) and spread (further
        // away). Each row draws `magazineSize` placeholder pips at low
        // alpha; the first `ammo` of them are overlaid in full slot
        // color. During reload, the entire row is overlaid in a single
        // continuous low-alpha track (visually: "the weapon is being
        // recharged"). The visual is small enough to read alongside
        // the HP bar without crowding the corner.
        const auto drawAmmoRow = [&](float rowY,
                                     int weaponKind,
                                     std::uint16_t magSize,
                                     std::uint16_t ammo,
                                     std::uint16_t reloadIn) {
            // Glyph color matches the pip color used for the row state
            // so the icon reads as "this weapon, currently …":
            //   * reloading  → dim alpha (matches the reload track)
            //   * ready      → full slot color
            const std::uint32_t glyphColor = reloadIn > 0
                ? (color & 0x00FFFFFFu) |
                      (static_cast<std::uint32_t>(kAmmoTrackAlpha8) << 24)
                : color;
            drawWeaponGlyph(rowY, weaponKind, glyphColor);

            for (std::uint16_t i = 0; i < magSize; ++i) {
                const float px =
                    cornerX + dir * (kWeaponGlyphWidthWU +
                                     kAmmoPipSize * 0.5f +
                                     static_cast<float>(i) * kAmmoPipSpacing);
                threadmaxx::DebugPoint dp{};
                dp.position   = {px, rowY, 0.0f};
                dp.pixelSize  = kAmmoPipSize;
                dp.cameraMask = slotCameraMask;
                if (reloadIn > 0) {
                    // Reload in progress — paint every slot at low
                    // alpha so the row reads as "occupied but not
                    // available."
                    const std::uint32_t a8 = static_cast<std::uint32_t>(kAmmoTrackAlpha8);
                    dp.colorRGBA = (color & 0x00FFFFFFu) | (a8 << 24);
                } else if (i < ammo) {
                    dp.colorRGBA = color;
                } else {
                    // Magazine slot consumed but reload not yet started
                    // (the post-fire empty-but-eligible-next-tick edge
                    // case for the last bullet) — paint at dim alpha so
                    // the size of the magazine remains visible.
                    const std::uint32_t a8 = static_cast<std::uint32_t>(kAmmoTrackAlpha8);
                    dp.colorRGBA = (color & 0x00FFFFFFu) | (a8 << 24);
                }
                b.addDebugPoint(dp);
            }
        };

        const float dumbY    = barY    - kAmmoRowGapWU;
        const float specialY = dumbY  - kAmmoRowGapWU;
        drawAmmoRow(dumbY,  /*weaponKind=*/0, kDumbfireMagazine,
                    state.dumbfireAmmo, state.dumbfireReload);
        // M5.6 — per-kind mag size + bullet color from the spec table.
        const SpecialWeaponSpec& sspec = specialSpecAt(state.specialKind);
        drawAmmoRow(specialY, sspec.weaponKind, sspec.magazine,
                    state.specialAmmo, state.specialReload);
    }

    // ---- N2 RepairKit glyphs -----------------------------------------
    // Cyan "+" cross drawn at every active kit's world position. Visible
    // from every camera (`cameraMask = 0xFFFFFFFFu`, the default) so each
    // human's viewport shows the kits in its frustum. Distinct from the
    // green RepairBase TILE rendered into the terrain JPG — the cross
    // shape + cyan tint reads as "collectible / restock kit" rather than
    // "static green tile to stand on."
    //
    // Geometry: two perpendicular DebugLine segments. Arm length scales
    // with hudScale so the glyph stays legible at every accessibility
    // setting. Size is tuned to be visible at default camera zoom but
    // not crowd the frame.
    {
        constexpr std::uint32_t kKitGlyphColor = 0xFFFFC080u;  // cyan-ish
        constexpr float         kBaseArmLenWU  = 6.0f;
        const float armLen = kBaseArmLenWU * sc;
        for (const auto& [kx, ky] : kitPositionsXY_) {
            threadmaxx::DebugLine h{};
            h.a         = {kx - armLen, ky, 0.0f};
            h.b         = {kx + armLen, ky, 0.0f};
            h.colorRGBA = kKitGlyphColor;
            b.addDebugLine(h);
            threadmaxx::DebugLine v{};
            v.a         = {kx, ky - armLen, 0.0f};
            v.b         = {kx, ky + armLen, 0.0f};
            v.colorRGBA = kKitGlyphColor;
            b.addDebugLine(v);
        }
    }

    // ---- M4.2 winner banner -------------------------------------------
    // Centered at the camera follow point; outlined in white; the
    // winner's slot color forms a large central badge surrounded by a
    // row of pips for the final kill count. No text (the renderer
    // doesn't draw DebugText today) — color + pip count carries the
    // information.
    if (roundEnded_ && roundEnded_->load(std::memory_order_acquire) &&
        winnerSlot_ != nullptr && winnerKills_ != nullptr) {
        const std::uint8_t  slot  = *winnerSlot_;
        const std::uint16_t kills = *winnerKills_;
        const std::uint32_t wcolor =
            slot < kSlotColors.size() ? kSlotColors[slot] : kBannerWhite;

        // M5.1 — banner anchors to the first human's viewport center.
        // With split-screen, P1's view shows the banner; other humans
        // see it through their own viewport if it crosses their world
        // rect (close-by humans during the round will share visibility).
        const threadmaxx::Vec3 bannerCenter = camera_->followCenter(0);
        const float kBannerHalfWidthWU  = kBaseBannerHalfWidthWU  * sc;
        const float kBannerHalfHeightWU = kBaseBannerHalfHeightWU * sc;
        const float kBannerBadgeSize    = kBaseBannerBadgeSize    * sc;
        const float kBannerKillsPipSize = kBaseBannerKillsPipSize * sc;
        const float minX = bannerCenter.x - kBannerHalfWidthWU;
        const float maxX = bannerCenter.x + kBannerHalfWidthWU;
        const float minY = bannerCenter.y - kBannerHalfHeightWU;
        const float maxY = bannerCenter.y + kBannerHalfHeightWU;
        const auto outlineSegment = [&](float ax, float ay, float bx, float by) {
            threadmaxx::DebugLine ln{};
            ln.a         = {ax, ay, 0.0f};
            ln.b         = {bx, by, 0.0f};
            ln.colorRGBA = kBannerWhite;
            b.addDebugLine(ln);
        };
        outlineSegment(minX, minY, maxX, minY);
        outlineSegment(maxX, minY, maxX, maxY);
        outlineSegment(maxX, maxY, minX, maxY);
        outlineSegment(minX, maxY, minX, minY);

        // Winner badge (large slot-colored point) left-of-center.
        {
            threadmaxx::DebugPoint dp{};
            dp.position  = {
                bannerCenter.x - kBannerHalfWidthWU * 0.55f,
                bannerCenter.y, 0.0f,
            };
            dp.colorRGBA = wcolor;
            dp.pixelSize = kBannerBadgeSize;
            b.addDebugPoint(dp);
        }

        // Kill count expressed as pips to the right of the badge.
        const std::uint32_t pipKills =
            std::min<std::uint32_t>(kills, kMaxScorePips);
        for (std::uint32_t i = 0; i < pipKills; ++i) {
            threadmaxx::DebugPoint dp{};
            dp.position  = {
                bannerCenter.x - kBannerHalfWidthWU * 0.30f +
                    static_cast<float>(i) * (kBannerKillsPipSize * 1.6f),
                bannerCenter.y, 0.0f,
            };
            dp.colorRGBA = wcolor;
            dp.pixelSize = kBannerKillsPipSize;
            b.addDebugPoint(dp);
        }
    }
}

} // namespace tou2d
