#pragma once

// tou2d::ui — M6.0 TTF runtime-baked font atlas.
//
// Drop-in contract: any TTF/OTF that covers the ASCII range
// 0x20..0x7E loads and renders without code change. Non-ASCII
// codepoints are opt-in via FontConfig::codepoints. The actual
// TTF asset lives at examples/tou2d/assets/ui/font.ttf (DejaVu
// Sans Mono by default; replace freely — see
// examples/tou2d/assets/ui/README.md).
//
// Pipeline:
//   bakeFont(ttfBytes, FontConfig) -> FontAtlas
//     ^ one-time at startup per pixel-size tier (M6.5's UI scale
//       slider bakes one atlas per tier so glyphs stay crisp).
//   atlas.lookupGlyph(codepoint) -> const GlyphMetrics*
//     ^ used by tou2d::ui::textPrintf to walk a string.
//
// The atlas pixel buffer is an R8 alpha texture; the renderer
// uploads it once and reads alpha into the Overlay-lane fragment
// shader. Color comes from the per-DrawItem tint.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tou2d::ui {

/// Per-glyph metrics in the baked atlas. Pixel coordinates,
/// integer-truncated from stb_truetype's 26.6 fixed-point output.
struct GlyphMetrics {
    std::uint16_t codepoint;  ///< Unicode codepoint this glyph represents.
    std::int16_t  u0;         ///< Atlas left edge in pixels.
    std::int16_t  v0;         ///< Atlas top edge in pixels.
    std::int16_t  u1;         ///< Atlas right edge in pixels.
    std::int16_t  v1;         ///< Atlas bottom edge in pixels.
    std::int16_t  xoff;       ///< Pen-relative draw offset (x).
    std::int16_t  yoff;       ///< Pen-relative draw offset (y) — baseline-relative.
    std::int16_t  xadvance;   ///< Pixel advance after this glyph.
};
static_assert(sizeof(GlyphMetrics) == 16,
              "GlyphMetrics layout pinned — bumping this size affects "
              "the font atlas baking memory budget assertion in tests.");

/// Bake configuration. Pass to bakeFont() alongside the raw TTF bytes.
struct FontConfig {
    /// Glyph pixel height. Picked per UI-scale tier (M6.5).
    int pixelSize = 16;

    /// Codepoint ranges to bake. Each pair is [first, last] inclusive.
    /// Default: ASCII printable range.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> codepoints =
        {{0x20u, 0x7Eu}};

    /// Atlas dimensions in pixels. The bake walks each codepoint range
    /// and packs greedily; if a range overflows the atlas, bakeFont()
    /// fails. Default 512×512 fits ASCII at 32 px with room to spare.
    int atlasWidth  = 512;
    int atlasHeight = 512;
};

/// The baked font atlas. Owns its pixel buffer + glyph metrics.
struct FontAtlas {
    int pixelSize = 0;
    int atlasW    = 0;
    int atlasH    = 0;
    int ascent    = 0;  ///< Baseline-to-top in pixels.
    int descent   = 0;  ///< Baseline-to-bottom in pixels (negative).
    int lineGap   = 0;  ///< Extra line-to-line padding.

    /// R8 alpha buffer; size == atlasW * atlasH.
    std::vector<std::byte> pixels;

    /// Glyphs sorted by codepoint (so lookupGlyph could do binary
    /// search; we use the hashmap for O(1) instead).
    std::vector<GlyphMetrics> glyphs;

    /// codepoint -> index in `glyphs`. Built by bakeFont().
    std::unordered_map<std::uint32_t, std::uint32_t> indexByCp;

    /// O(1) glyph lookup. Returns nullptr if the codepoint wasn't baked.
    [[nodiscard]] const GlyphMetrics* lookupGlyph(std::uint32_t cp) const {
        const auto it = indexByCp.find(cp);
        if (it == indexByCp.end()) return nullptr;
        return &glyphs[it->second];
    }

    /// Did the bake produce a usable atlas? False on empty input or
    /// pack failure (codepoint range overflowed the atlas).
    [[nodiscard]] bool valid() const {
        return atlasW > 0 && atlasH > 0 && !pixels.empty() && !glyphs.empty();
    }
};

/// Bake a TTF/OTF byte buffer into a FontAtlas. Returns an empty
/// (invalid) atlas on failure (bad TTF, or the requested codepoint
/// range overflowed the atlas dimensions).
///
/// Thread-safety: pure function, no globals. Safe to call from any
/// thread.
///
/// Cost: O(N) over the codepoint range; the dominant cost is
/// stb_truetype's per-glyph rasterization. For the default ASCII
/// range at 16 px this is < 5 ms on a modern x86.
[[nodiscard]] FontAtlas bakeFont(std::span<const std::byte> ttfBytes,
                                 const FontConfig& cfg);

/// Convenience: read a TTF file from disk and bake it. Returns an
/// invalid atlas if the file cannot be read.
[[nodiscard]] FontAtlas bakeFontFromFile(std::string_view path,
                                         const FontConfig& cfg);

}  // namespace tou2d::ui
