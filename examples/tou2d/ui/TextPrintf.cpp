// tou2d::ui — measureText implementation. textPrintf is templated
// and inlined in the header.

#include "TextPrintf.hpp"

#include <algorithm>

namespace tou2d::ui {

TextBox measureText(const FontAtlas& atlas, std::string_view text) {
    TextBox box{};
    if (!atlas.valid() || text.empty()) return box;

    // Sweep with a phantom textPrintf — emit ignores the quads, we
    // track the running min/max corner instead. Cheaper to inline the
    // sweep directly than to introduce a callback-based collector
    // path; the layout math is the same.
    float penX = 0.0f;
    const float baseY = static_cast<float>(atlas.ascent);  // baseline at ascent
    float penY = baseY;
    const float lh = lineHeight(atlas);

    bool anyGlyph = false;
    box.x0 = box.y0 =  1e30f;
    box.x1 = box.y1 = -1e30f;

    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto byte = static_cast<std::uint8_t>(text[i]);
        if (byte == '\n') {
            penX = 0.0f;
            penY += lh;
            continue;
        }
        const std::uint32_t cp = byte;
        const GlyphMetrics* g = atlas.lookupGlyph(cp);
        if (g == nullptr) continue;

        const bool hasRect = (g->u1 > g->u0) && (g->v1 > g->v0);
        if (hasRect) {
            const float x0 = penX + static_cast<float>(g->xoff);
            const float y0 = penY + static_cast<float>(g->yoff);
            const float x1 = x0 + static_cast<float>(g->u1 - g->u0);
            const float y1 = y0 + static_cast<float>(g->v1 - g->v0);
            box.x0 = std::min(box.x0, x0);
            box.y0 = std::min(box.y0, y0);
            box.x1 = std::max(box.x1, x1);
            box.y1 = std::max(box.y1, y1);
            anyGlyph = true;
        }
        penX += static_cast<float>(g->xadvance);
    }

    if (!anyGlyph) {
        // SPACE-only string etc. — return zero-height box at origin.
        box = TextBox{};
    }
    return box;
}

}  // namespace tou2d::ui
