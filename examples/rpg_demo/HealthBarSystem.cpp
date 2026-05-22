#include "HealthBarSystem.hpp"

#include <threadmaxx/Query.hpp>
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
// 2026-05-22 audit (round 2) — collapsed the 3-line stack into a
// single bar and widened it (kBarHalfWidth 0.55 → 0.95). The
// debug-line renderer doesn't expose per-segment thickness, so
// "thicker" comes from "longer" (more horizontal pixels at the
// same screen distance) + a slightly higher overlay altitude.
constexpr float kBarHalfWidth   = 0.95f;
constexpr float kBarHeight      = 1.65f;   // above entity origin

inline std::uint32_t hpColor(float frac) {
    if (frac < 0.33f) return 0xFF4040FFu;   // red    (ABGR)
    if (frac < 0.66f) return 0xFF40D0FFu;   // yellow
    return 0xFF40FF40u;                     // green
}

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

    const auto chunkCount = w.archetypeChunkCount();
    for (std::size_t i = 0; i < chunkCount; ++i) {
        const auto& chunk = w.archetypeChunk(i);
        if (!chunk.mask.has(threadmaxx::Component::Health)) continue;
        if (!chunk.mask.has(threadmaxx::Component::Transform)) continue;
        if (chunk.mask.has(threadmaxx::Component::DisabledTag)) continue;
        const auto n = chunk.entities.size();
        for (std::size_t row = 0; row < n; ++row) {
            const auto& hp = chunk.healths[row];
            if (hp.current >= hp.max) continue;  // full HP: no bar
            if (hp.current <= 0.0f)   continue;  // already dead
            const auto& t = chunk.transforms[row];
            const float frac = hp.current / hp.max;
            const float cx = t.position.x;
            const float cy = t.position.y + kBarHeight;
            const float cz = t.position.z;

            // Bar endpoints span ±kBarHalfWidth along the camera-right
            // basis; the midpoint marks the HP fill boundary.
            const float lx = cx - rightBasis.x * kBarHalfWidth;
            const float lz = cz - rightBasis.z * kBarHalfWidth;
            const float rx = cx + rightBasis.x * kBarHalfWidth;
            const float rz = cz + rightBasis.z * kBarHalfWidth;
            const float fillSpan = 2.0f * kBarHalfWidth * frac;
            const float mx = lx + rightBasis.x * fillSpan;
            const float mz = lz + rightBasis.z * fillSpan;

            // Single billboarded bar: dark grey background + the
            // proportional fill segment. (The debug-line renderer
            // does not expose per-segment thickness; making the bar
            // visibly "wider" comes from a longer kBarHalfWidth.)
            constexpr std::uint32_t kBg = 0xFF202020u;  // dark grey
            const std::uint32_t fg = hpColor(frac);
            threadmaxx::DebugLine bgLine;
            bgLine.a = {lx, cy, lz};
            bgLine.b = {rx, cy, rz};
            bgLine.colorRGBA = kBg;
            b.addDebugLine(bgLine);
            threadmaxx::DebugLine fgLine;
            fgLine.a = {lx, cy, lz};
            fgLine.b = {mx, cy, mz};
            fgLine.colorRGBA = fg;
            b.addDebugLine(fgLine);
        }
    }
}

} // namespace rpg
