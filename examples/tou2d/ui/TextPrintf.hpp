#pragma once

// tou2d::ui — M6.0 text layout + emit.
//
// textPrintf walks a UTF-8-ish string, looks up each codepoint's
// GlyphMetrics, computes screen-space pixel rects + atlas UV rects,
// and hands each visible glyph to a caller-provided emit callback.
// Newlines (\n) advance the pen one line and reset x to baseX.
//
// The split is deliberately renderer-neutral: the layout math lives
// here; the actual glyph-quad pipeline lives in whichever consumer
// you wire up (DrawItem on RenderPass::Overlay for the production
// path; a debug-line outline for a quick visual sanity check; a
// test collector for tou2d_font_atlas_test).
//
// v1 decoder: 7-bit ASCII passes through; high-bit bytes are
// treated as Latin-1 codepoints. Full UTF-8 decode is a follow-up
// — DejaVu's ASCII coverage is what the HUD needs.

#include "Font.hpp"

#include <cstdint>
#include <string_view>

namespace tou2d::ui {

/// Pen position after a textPrintf or measureText call.
struct PenPos {
    float x = 0.0f;
    float y = 0.0f;
};

/// One emitted glyph: screen-space pixel rect + atlas UV rect + color.
struct TextGlyphQuad {
    float         x0     = 0.0f;
    float         y0     = 0.0f;
    float         x1     = 0.0f;
    float         y1     = 0.0f;
    float         u0     = 0.0f;
    float         v0     = 0.0f;
    float         u1     = 0.0f;
    float         v1     = 0.0f;
    std::uint32_t color  = 0xFFFFFFFFu;
};

/// Pixel bbox of a measureText() call.
struct TextBox {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    [[nodiscard]] float width()  const { return x1 - x0; }
    [[nodiscard]] float height() const { return y1 - y0; }
};

/// Vertical advance between two baselines for this font.
[[nodiscard]] inline float lineHeight(const FontAtlas& atlas) {
    return static_cast<float>(atlas.ascent - atlas.descent + atlas.lineGap);
}

/// Walk `text`, emitting one TextGlyphQuad per visible glyph via
/// `emit`. The pen starts at (`baseX`, `baseY`) where `baseY` is the
/// BASELINE Y in screen pixels (ascent above, descent below).
/// `\n` advances one line; missing-glyph codepoints are silently
/// skipped (still consuming x advance for SPACE-like glyphs).
///
/// `emit` is any callable matching `void(const TextGlyphQuad&)`.
/// Inlined at the call site; no std::function overhead.
template <class Emit>
PenPos textPrintf(const FontAtlas& atlas,
                  float baseX,
                  float baseY,
                  std::uint32_t color,
                  std::string_view text,
                  Emit&& emit) {
    if (!atlas.valid()) return PenPos{baseX, baseY};

    float penX = baseX;
    float penY = baseY;
    const float lh = lineHeight(atlas);

    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto byte = static_cast<std::uint8_t>(text[i]);
        if (byte == '\n') {
            penX = baseX;
            penY += lh;
            continue;
        }
        const std::uint32_t cp = byte;  // ASCII / Latin-1
        const GlyphMetrics* g = atlas.lookupGlyph(cp);
        if (g == nullptr) {
            // Try '?' as a fallback advance; otherwise skip.
            const GlyphMetrics* q = atlas.lookupGlyph('?');
            if (q != nullptr) penX += static_cast<float>(q->xadvance);
            continue;
        }

        // Empty-rect glyphs (e.g. SPACE) still advance.
        const bool hasRect = (g->u1 > g->u0) && (g->v1 > g->v0);
        if (hasRect) {
            TextGlyphQuad q{};
            q.x0 = penX + static_cast<float>(g->xoff);
            q.y0 = penY + static_cast<float>(g->yoff);
            q.x1 = q.x0 + static_cast<float>(g->u1 - g->u0);
            q.y1 = q.y0 + static_cast<float>(g->v1 - g->v0);
            const float invW = 1.0f / static_cast<float>(atlas.atlasW);
            const float invH = 1.0f / static_cast<float>(atlas.atlasH);
            q.u0 = static_cast<float>(g->u0) * invW;
            q.v0 = static_cast<float>(g->v0) * invH;
            q.u1 = static_cast<float>(g->u1) * invW;
            q.v1 = static_cast<float>(g->v1) * invH;
            q.color = color;
            emit(q);
        }
        penX += static_cast<float>(g->xadvance);
    }
    return PenPos{penX, penY};
}

/// Pixel bbox of `text` rendered at origin (0, baseline=ascent).
/// Returns an empty box for empty / invalid input.
[[nodiscard]] TextBox measureText(const FontAtlas& atlas,
                                  std::string_view text);

}  // namespace tou2d::ui
