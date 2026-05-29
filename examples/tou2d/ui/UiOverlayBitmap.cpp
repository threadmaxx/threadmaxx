#include "UiOverlayBitmap.hpp"

#include <algorithm>
#include <cstring>

namespace tou2d::ui {

namespace {

inline std::uint32_t packBytes(std::uint8_t a,
                               std::uint8_t b,
                               std::uint8_t g,
                               std::uint8_t r) noexcept {
    return (std::uint32_t{a} << 24) |
           (std::uint32_t{b} << 16) |
           (std::uint32_t{g} <<  8) |
           (std::uint32_t{r} <<  0);
}

inline void unpackBytes(std::uint32_t rgba,
                        std::uint8_t& r,
                        std::uint8_t& g,
                        std::uint8_t& b,
                        std::uint8_t& a) noexcept {
    r = static_cast<std::uint8_t>((rgba >>  0) & 0xFFu);
    g = static_cast<std::uint8_t>((rgba >>  8) & 0xFFu);
    b = static_cast<std::uint8_t>((rgba >> 16) & 0xFFu);
    a = static_cast<std::uint8_t>((rgba >> 24) & 0xFFu);
}

}  // namespace

std::uint32_t UiOverlayBitmap::blendOver(std::uint32_t dst,
                                         std::uint32_t src) noexcept {
    std::uint8_t sr, sg, sb, sa;
    unpackBytes(src, sr, sg, sb, sa);
    if (sa == 0) return dst;
    if (sa == 255) return src;

    std::uint8_t dr, dg, db, da;
    unpackBytes(dst, dr, dg, db, da);

    // Straight-alpha over composite (Porter-Duff "over").
    //   outA = sa + da * (1 - sa)
    //   outRGB = (sa * srcRGB + (1 - sa) * da * dstRGB) / outA
    const std::uint32_t invSa = 255u - sa;
    const std::uint32_t outA  = sa + (da * invSa + 127u) / 255u;
    if (outA == 0) return 0;

    const std::uint32_t outR =
        (std::uint32_t{sr} * sa * 255u +
         std::uint32_t{dr} * da * invSa) /
        (outA * 255u);
    const std::uint32_t outG =
        (std::uint32_t{sg} * sa * 255u +
         std::uint32_t{dg} * da * invSa) /
        (outA * 255u);
    const std::uint32_t outB =
        (std::uint32_t{sb} * sa * 255u +
         std::uint32_t{db} * da * invSa) /
        (outA * 255u);

    return packBytes(static_cast<std::uint8_t>(outA),
                     static_cast<std::uint8_t>(outB),
                     static_cast<std::uint8_t>(outG),
                     static_cast<std::uint8_t>(outR));
}

void UiOverlayBitmap::resize(std::uint32_t width, std::uint32_t height) {
    width_  = width;
    height_ = height;
    pixels_.assign(static_cast<std::size_t>(width) *
                       static_cast<std::size_t>(height),
                   0u);
    dirty_ = DirtyRect{0, 0, width, height};
}

void UiOverlayBitmap::clear(std::uint32_t rgba) noexcept {
    if (pixels_.empty()) return;
    std::fill(pixels_.begin(), pixels_.end(), rgba);
    dirty_ = DirtyRect{0, 0, width_, height_};
}

void UiOverlayBitmap::clearRect(std::int32_t x, std::int32_t y,
                                std::uint32_t w, std::uint32_t h,
                                std::uint32_t rgba) noexcept {
    if (pixels_.empty()) return;

    std::int32_t x0 = std::max<std::int32_t>(0, x);
    std::int32_t y0 = std::max<std::int32_t>(0, y);
    std::int32_t x1 = std::min<std::int32_t>(static_cast<std::int32_t>(width_),
                                             x + static_cast<std::int32_t>(w));
    std::int32_t y1 = std::min<std::int32_t>(static_cast<std::int32_t>(height_),
                                             y + static_cast<std::int32_t>(h));
    if (x1 <= x0 || y1 <= y0) return;

    for (std::int32_t row = y0; row < y1; ++row) {
        std::uint32_t* line = pixels_.data() +
                              static_cast<std::size_t>(row) * width_;
        std::fill(line + x0, line + x1, rgba);
    }
    unionDirty(x0, y0,
               static_cast<std::uint32_t>(x1 - x0),
               static_cast<std::uint32_t>(y1 - y0));
}

void UiOverlayBitmap::blitGlyphR8Alpha(const std::byte* srcAlpha,
                                       std::uint32_t srcW, std::uint32_t srcH,
                                       std::uint32_t srcStride,
                                       std::int32_t dstX, std::int32_t dstY,
                                       std::uint32_t tintRgba) noexcept {
    if (pixels_.empty() || srcAlpha == nullptr || srcW == 0 || srcH == 0) return;

    // Clip destination.
    std::int32_t dx0 = std::max<std::int32_t>(0, dstX);
    std::int32_t dy0 = std::max<std::int32_t>(0, dstY);
    std::int32_t dx1 = std::min<std::int32_t>(static_cast<std::int32_t>(width_),
                                              dstX + static_cast<std::int32_t>(srcW));
    std::int32_t dy1 = std::min<std::int32_t>(static_cast<std::int32_t>(height_),
                                              dstY + static_cast<std::int32_t>(srcH));
    if (dx1 <= dx0 || dy1 <= dy0) return;

    std::uint8_t tr, tg, tb, ta;
    unpackBytes(tintRgba, tr, tg, tb, ta);
    if (ta == 0) return;  // fully transparent tint → nothing to draw

    for (std::int32_t dy = dy0; dy < dy1; ++dy) {
        const std::int32_t sy = dy - dstY;
        const std::byte*   srcRow = srcAlpha +
                                    static_cast<std::size_t>(sy) * srcStride;
        std::uint32_t* dstRow = pixels_.data() +
                                static_cast<std::size_t>(dy) * width_;

        for (std::int32_t dx = dx0; dx < dx1; ++dx) {
            const std::int32_t sx = dx - dstX;
            const std::uint8_t glyphA = static_cast<std::uint8_t>(srcRow[sx]);
            if (glyphA == 0) continue;

            // Combine tint alpha with glyph alpha: srcA = ta * glyphA / 255.
            const std::uint32_t srcA = (std::uint32_t{ta} * glyphA + 127u) / 255u;
            const std::uint32_t src = packBytes(static_cast<std::uint8_t>(srcA),
                                                tb, tg, tr);
            dstRow[dx] = blendOver(dstRow[dx], src);
        }
    }
    unionDirty(dx0, dy0,
               static_cast<std::uint32_t>(dx1 - dx0),
               static_cast<std::uint32_t>(dy1 - dy0));
}

PenPos UiOverlayBitmap::drawText(const FontAtlas& atlas,
                                 float baseX, float baseY,
                                 std::uint32_t color,
                                 std::string_view text) noexcept {
    // textPrintf emits per-glyph rects in atlas UV space (0..1). The
    // compositor needs pixel-space rects into the atlas to slice the
    // R8 alpha buffer. Convert back from normalized UVs by
    // multiplying by atlas dims. Empty / invalid atlas → no-op.
    if (!atlas.valid()) return PenPos{baseX, baseY};

    const std::byte* atlasBase = atlas.pixels.data();
    const std::uint32_t atlasStride = static_cast<std::uint32_t>(atlas.atlasW);

    return textPrintf(atlas, baseX, baseY, color, text,
        [&](const TextGlyphQuad& q) {
            // Pixel rect in atlas:
            const std::uint32_t u0 = static_cast<std::uint32_t>(
                q.u0 * static_cast<float>(atlas.atlasW) + 0.5f);
            const std::uint32_t v0 = static_cast<std::uint32_t>(
                q.v0 * static_cast<float>(atlas.atlasH) + 0.5f);
            const std::uint32_t u1 = static_cast<std::uint32_t>(
                q.u1 * static_cast<float>(atlas.atlasW) + 0.5f);
            const std::uint32_t v1 = static_cast<std::uint32_t>(
                q.v1 * static_cast<float>(atlas.atlasH) + 0.5f);
            if (u1 <= u0 || v1 <= v0) return;

            const std::uint32_t glyphW = u1 - u0;
            const std::uint32_t glyphH = v1 - v0;
            const std::byte* srcBase = atlasBase +
                                       static_cast<std::size_t>(v0) * atlasStride +
                                       static_cast<std::size_t>(u0);
            const std::int32_t dstX = static_cast<std::int32_t>(q.x0 + 0.5f);
            const std::int32_t dstY = static_cast<std::int32_t>(q.y0 + 0.5f);
            blitGlyphR8Alpha(srcBase, glyphW, glyphH, atlasStride,
                             dstX, dstY, q.color);
        });
}

bool UiOverlayBitmap::extractRegion(const DirtyRect& rect,
                                    std::span<std::uint8_t> dst) const noexcept {
    if (rect.empty()) return false;
    if (rect.x + rect.w > width_ || rect.y + rect.h > height_) return false;
    const std::size_t need = static_cast<std::size_t>(rect.w) *
                             static_cast<std::size_t>(rect.h) * 4u;
    if (dst.size() < need) return false;

    for (std::uint32_t row = 0; row < rect.h; ++row) {
        const std::uint32_t* srcRow = pixels_.data() +
                                      static_cast<std::size_t>(rect.y + row) * width_ +
                                      rect.x;
        std::uint8_t* dstRow = dst.data() +
                               static_cast<std::size_t>(row) * rect.w * 4u;
        std::memcpy(dstRow, srcRow, static_cast<std::size_t>(rect.w) * 4u);
    }
    return true;
}

void UiOverlayBitmap::unionDirty(std::int32_t x, std::int32_t y,
                                 std::uint32_t w, std::uint32_t h) noexcept {
    if (w == 0 || h == 0) return;
    const std::uint32_t ux = static_cast<std::uint32_t>(std::max<std::int32_t>(0, x));
    const std::uint32_t uy = static_cast<std::uint32_t>(std::max<std::int32_t>(0, y));
    const std::uint32_t ux1 = std::min<std::uint32_t>(width_,  ux + w);
    const std::uint32_t uy1 = std::min<std::uint32_t>(height_, uy + h);
    if (ux1 <= ux || uy1 <= uy) return;

    if (dirty_.empty()) {
        dirty_ = DirtyRect{ux, uy, ux1 - ux, uy1 - uy};
        return;
    }
    const std::uint32_t dx0 = std::min(dirty_.x, ux);
    const std::uint32_t dy0 = std::min(dirty_.y, uy);
    const std::uint32_t dx1 = std::max(dirty_.x + dirty_.w, ux1);
    const std::uint32_t dy1 = std::max(dirty_.y + dirty_.h, uy1);
    dirty_ = DirtyRect{dx0, dy0, dx1 - dx0, dy1 - dy0};
}

}  // namespace tou2d::ui
