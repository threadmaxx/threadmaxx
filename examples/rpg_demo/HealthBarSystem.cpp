#include "HealthBarSystem.hpp"

#include <threadmaxx/Query.hpp>
#include <threadmaxx/UserComponent.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>

#include <cmath>

namespace rpg {

namespace {

// 2026-05-20 — HP bar rendered as a debug line. The Vulkan
// reference renderer doesn't yet implement debug-text
// rasterisation, so the prior `addDebugText` version was silent.
//
// 2026-05-22 audit fix — bar is camera-billboarded; we take
// `right = normalize(cross(camera.forward, worldUp))` projected
// onto XZ so it always faces the main camera.
//
// 2026-05-22 audit (round 3) — bars are visibly thicker. The
// debug-line API doesn't expose per-segment thickness, so we stack
// `kBarStackLines` near-parallel segments offset by
// `kBarStackStep` in Y. The HP bar sits at `kBarHeightHp`; the
// stamina bar (PLAYER ONLY) sits below it at `kBarHeightStamina`.
// Half-width stays 0.95 from the previous round.
constexpr float        kBarHalfWidth   = 0.95f;
constexpr float        kBarHeightHp      = 1.85f;   // above entity origin
constexpr float        kBarHeightStamina = 1.62f;   // below the HP bar
constexpr float        kBarStackStep     = 0.035f;  // vertical pitch between stacked lines
constexpr int          kBarStackLines    = 5;       // # of stacked lines per bar

inline std::uint32_t hpColor(float frac) {
    if (frac < 0.33f) return 0xFF4040FFu;   // red    (ABGR)
    if (frac < 0.66f) return 0xFF40D0FFu;   // yellow
    return 0xFF40FF40u;                     // green
}

constexpr std::uint32_t kBgColor      = 0xFF202020u;  // dark grey
constexpr std::uint32_t kStaminaColor = 0xFFE0B040u;  // sky-blue stamina (ABGR)

} // namespace

void HealthBarSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    if (!world_) return;
    const auto& w = *world_;

    // 2026-05-22 audit fix — pull the main camera's right basis
    // from `worldState_->activeCameras[0]`. The camera's forward
    // points from eye toward target; right = normalize(cross(
    // forward, worldUp)). For a top-down minimap or no-camera
    // edge case, fall back to world +X (legacy behaviour).
    threadmaxx::Vec3 rightBasis{1.0f, 0.0f, 0.0f};
    if (worldState_ && !worldState_->activeCameras.empty()) {
        const auto& cam = worldState_->activeCameras.front();
        const threadmaxx::Vec3 f = cam.forward;
        const float fl = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
        if (fl > 1e-4f) {
            // right = normalize(cross(forward, worldUp))
            const float fxn = f.x / fl;
            const float fyn = f.y / fl;
            const float fzn = f.z / fl;
            // worldUp = (0, 1, 0) → cross(f, up) = (fz, 0, -fx)
            float rx =  fzn;
            float ry =  0.0f;
            float rz = -fxn;
            const float rl = std::sqrt(rx * rx + rz * rz);
            if (rl > 1e-4f) {
                rx /= rl; ry = 0.0f; rz /= rl;
                rightBasis = {rx, ry, rz};
            }
            (void)fyn;
        }
    }

    // 2026-05-22 audit (round 3) — emit a stacked bar (kBarStackLines
    // parallel segments offset in Y) at a given altitude.
    auto emitBar = [&](float cx, float cz,
                       float baseAltitude,
                       float fillFrac,
                       std::uint32_t bgColor,
                       std::uint32_t fgColor) {
        const float lx = cx - rightBasis.x * kBarHalfWidth;
        const float lz = cz - rightBasis.z * kBarHalfWidth;
        const float rx = cx + rightBasis.x * kBarHalfWidth;
        const float rz = cz + rightBasis.z * kBarHalfWidth;
        const float clampedFrac = std::max(0.0f, std::min(1.0f, fillFrac));
        const float fillSpan = 2.0f * kBarHalfWidth * clampedFrac;
        const float mx = lx + rightBasis.x * fillSpan;
        const float mz = lz + rightBasis.z * fillSpan;
        // Center the stack on `baseAltitude` so adding more lines
        // keeps the bar's apparent middle stable.
        const float stackTotal = kBarStackStep *
            static_cast<float>(kBarStackLines - 1);
        const float bottom = baseAltitude - stackTotal * 0.5f;
        for (int k = 0; k < kBarStackLines; ++k) {
            const float y = bottom + kBarStackStep * static_cast<float>(k);
            threadmaxx::DebugLine bgLine;
            bgLine.a = {lx, y, lz};
            bgLine.b = {rx, y, rz};
            bgLine.colorRGBA = bgColor;
            b.addDebugLine(bgLine);
            if (clampedFrac > 0.0f) {
                threadmaxx::DebugLine fgLine;
                fgLine.a = {lx, y, lz};
                fgLine.b = {mx, y, mz};
                fgLine.colorRGBA = fgColor;
                b.addDebugLine(fgLine);
            }
        }
    };

    const auto playerH =
        worldState_ ? worldState_->player : threadmaxx::EntityHandle{};

    const auto chunkCount = w.archetypeChunkCount();
    for (std::size_t i = 0; i < chunkCount; ++i) {
        const auto& chunk = w.archetypeChunk(i);
        if (!chunk.mask.has(threadmaxx::Component::Health)) continue;
        if (!chunk.mask.has(threadmaxx::Component::Transform)) continue;
        if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;
        const auto n = chunk.entities.size();
        for (std::size_t row = 0; row < n; ++row) {
            const auto& hp = chunk.healths[row];
            if (hp.current <= 0.0f) continue;  // already dead
            const bool isPlayer = playerH.valid() &&
                                  chunk.entities[row] == playerH;
            // 2026-05-22 audit (round 3) — for the PLAYER always emit
            // both the HP bar and the stamina bar (even at full HP).
            // For NPCs keep the legacy "hide-at-full-HP" rule so the
            // mob crowd doesn't drown in always-on bars.
            if (!isPlayer && hp.current >= hp.max) continue;
            const auto& t = chunk.transforms[row];
            const float frac = hp.current / hp.max;
            const float cx = t.position.x;
            const float cz = t.position.z;

            emitBar(cx, cz, t.position.y + kBarHeightHp,
                    frac, kBgColor, hpColor(frac));

            if (isPlayer && ids_) {
                if (const auto* ps = threadmaxx::user::tryGet<PlayerState>(
                        w, ids_->playerState, playerH)) {
                    const float staminaFrac = std::max(0.0f,
                        std::min(1.0f, ps->stamina / kStaminaMax));
                    emitBar(cx, cz, t.position.y + kBarHeightStamina,
                            staminaFrac, kBgColor, kStaminaColor);
                }
            }
        }
    }
}

} // namespace rpg
