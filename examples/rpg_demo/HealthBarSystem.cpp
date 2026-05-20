#include "HealthBarSystem.hpp"

#include <threadmaxx/Query.hpp>
#include <threadmaxx/World.hpp>
#include <threadmaxx/render/RenderFrameBuilder.hpp>

namespace rpg {

namespace {

// 2026-05-20 — HP bar rendered as a stack of debug lines (the
// Vulkan reference renderer doesn't yet implement debug-text
// rasterisation, so the prior `addDebugText` version was silent —
// the user reported "no read indicator of HP"). We draw a 3-line
// vertical stack to give the bar visible thickness: a dim
// background segment + a proportionally-shorter foreground
// segment for the current HP. Colour: green > 66 %, yellow > 33 %,
// red below.
constexpr float kBarHalfWidth   = 0.55f;
constexpr float kBarHeight      = 1.55f;   // above entity origin
constexpr float kBarStackStep   = 0.05f;
constexpr int   kBarStackLines  = 3;

inline std::uint32_t hpColor(float frac) {
    if (frac < 0.33f) return 0xFF4040FFu;   // red    (ABGR)
    if (frac < 0.66f) return 0xFF40D0FFu;   // yellow
    return 0xFF40FF40u;                     // green
}

} // namespace

void HealthBarSystem::buildRenderFrame(threadmaxx::RenderFrameBuilder& b) {
    if (!world_) return;
    const auto& w = *world_;
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
            const float xL = t.position.x - kBarHalfWidth;
            const float xR = t.position.x + kBarHalfWidth;
            const float xMid = xL + (xR - xL) * frac;
            const float y0 = t.position.y + kBarHeight;
            const float z  = t.position.z;

            // Stack of horizontal lines = thick bar.
            constexpr std::uint32_t kBg = 0xFF202020u;  // dark grey
            const std::uint32_t fg = hpColor(frac);
            for (int k = 0; k < kBarStackLines; ++k) {
                const float y = y0 + k * kBarStackStep;
                threadmaxx::DebugLine bgLine;
                bgLine.a = {xL, y, z};
                bgLine.b = {xR, y, z};
                bgLine.colorRGBA = kBg;
                b.addDebugLine(bgLine);
                threadmaxx::DebugLine fgLine;
                fgLine.a = {xL,   y, z};
                fgLine.b = {xMid, y, z};
                fgLine.colorRGBA = fg;
                b.addDebugLine(fgLine);
            }
        }
    }
}

} // namespace rpg
