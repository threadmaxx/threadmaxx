// M6.0 — TTF runtime bake via stb_truetype.
//
// Uses stb_truetype's "packing" API (PackBegin / PackFontRanges /
// PackEnd) rather than the simpler BakeFontBitmap path. The packing
// API gives:
//   * proper sub-pixel positioning support (we don't use it yet, but
//     no work to enable later);
//   * better atlas packing (skyline algorithm vs naive row-major);
//   * per-range glyph picking so non-ASCII codepoints land cleanly.
//
// We DO NOT use stb_truetype's kerning hooks in v1 — labels are
// short, kerning is invisible on most fixed-width display fonts, and
// adding kerning lookups to the textPrintf hot path adds cycles. Opt
// in when a real polish round needs it.

#include "Font.hpp"

#include <cstring>
#include <fstream>
#include <vector>

// stb_truetype is a header-only library. We compile its
// implementation here exactly once.
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

namespace tou2d::ui {

FontAtlas bakeFont(std::span<const std::byte> ttfBytes,
                   const FontConfig& cfg) {
    FontAtlas out{};

    // ---- Input validation ------------------------------------------------
    if (ttfBytes.empty() || cfg.pixelSize <= 0 ||
        cfg.atlasWidth <= 0 || cfg.atlasHeight <= 0 ||
        cfg.codepoints.empty()) {
        return out;
    }

    // Reject TTF buffers that don't start with a known sfnt signature.
    // stb_truetype will accept and crash on truly garbage input.
    if (ttfBytes.size() < 4) return out;
    const auto* head = reinterpret_cast<const std::uint8_t*>(ttfBytes.data());
    const bool isTtf =
        (head[0] == 0x00 && head[1] == 0x01 && head[2] == 0x00 && head[3] == 0x00) ||
        (head[0] == 'O'  && head[1] == 'T'  && head[2] == 'T'  && head[3] == 'O')  ||
        (head[0] == 't'  && head[1] == 't'  && head[2] == 'c'  && head[3] == 'f')  ||
        (head[0] == 't'  && head[1] == 'r'  && head[2] == 'u'  && head[3] == 'e');
    if (!isTtf) return out;

    // ---- Allocate the alpha bitmap --------------------------------------
    const std::size_t pixelCount =
        static_cast<std::size_t>(cfg.atlasWidth) *
        static_cast<std::size_t>(cfg.atlasHeight);
    std::vector<std::byte> pixels(pixelCount, std::byte{0});

    // ---- Pack ranges -----------------------------------------------------
    stbtt_pack_context pc{};
    if (!stbtt_PackBegin(
            &pc,
            reinterpret_cast<unsigned char*>(pixels.data()),
            cfg.atlasWidth,
            cfg.atlasHeight,
            /*stride*/ 0,
            /*padding*/ 1,
            /*alloc_context*/ nullptr)) {
        return out;
    }

    // 1× oversampling — DejaVu Sans Mono is already crisp at the
    // sizes we'll use (12-32 px). Bump to 2×2 if a future font needs
    // anti-aliased sub-pixel output.
    stbtt_PackSetOversampling(&pc, 1, 1);

    std::vector<std::vector<stbtt_packedchar>> rangeChars(cfg.codepoints.size());
    std::vector<stbtt_pack_range> ranges(cfg.codepoints.size());

    for (std::size_t i = 0; i < cfg.codepoints.size(); ++i) {
        const auto [first, last] = cfg.codepoints[i];
        if (last < first) {
            stbtt_PackEnd(&pc);
            return out;
        }
        const int n = static_cast<int>(last - first + 1);
        rangeChars[i].resize(static_cast<std::size_t>(n));
        ranges[i].font_size                        = static_cast<float>(cfg.pixelSize);
        ranges[i].first_unicode_codepoint_in_range = static_cast<int>(first);
        ranges[i].array_of_unicode_codepoints      = nullptr;
        ranges[i].num_chars                        = n;
        ranges[i].chardata_for_range               = rangeChars[i].data();
        ranges[i].h_oversample                     = 0;
        ranges[i].v_oversample                     = 0;
    }

    const int packOk = stbtt_PackFontRanges(
        &pc,
        reinterpret_cast<const unsigned char*>(ttfBytes.data()),
        /*fontIndex*/ 0,
        ranges.data(),
        static_cast<int>(ranges.size()));

    stbtt_PackEnd(&pc);

    if (!packOk) {
        // A range overflowed the atlas. The caller should bump
        // atlasWidth/Height or trim the codepoint set.
        return out;
    }

    // ---- Extract font-level metrics -------------------------------------
    stbtt_fontinfo info{};
    if (!stbtt_InitFont(
            &info,
            reinterpret_cast<const unsigned char*>(ttfBytes.data()),
            stbtt_GetFontOffsetForIndex(
                reinterpret_cast<const unsigned char*>(ttfBytes.data()), 0))) {
        return out;
    }
    int ascentUnits = 0, descentUnits = 0, lineGapUnits = 0;
    stbtt_GetFontVMetrics(&info, &ascentUnits, &descentUnits, &lineGapUnits);
    const float scale = stbtt_ScaleForPixelHeight(
        &info, static_cast<float>(cfg.pixelSize));

    out.pixelSize = cfg.pixelSize;
    out.atlasW    = cfg.atlasWidth;
    out.atlasH    = cfg.atlasHeight;
    out.ascent    = static_cast<int>(static_cast<float>(ascentUnits) * scale);
    out.descent   = static_cast<int>(static_cast<float>(descentUnits) * scale);
    out.lineGap   = static_cast<int>(static_cast<float>(lineGapUnits) * scale);
    out.pixels    = std::move(pixels);

    // ---- Flatten the per-range packed chars into GlyphMetrics ----------
    std::size_t totalGlyphs = 0;
    for (const auto& r : rangeChars) totalGlyphs += r.size();
    out.glyphs.reserve(totalGlyphs);
    out.indexByCp.reserve(totalGlyphs);

    for (std::size_t i = 0; i < cfg.codepoints.size(); ++i) {
        const auto [first, last] = cfg.codepoints[i];
        const auto& packed = rangeChars[i];
        for (std::size_t k = 0; k < packed.size(); ++k) {
            const auto& pc_ = packed[k];
            const std::uint32_t cp = first + static_cast<std::uint32_t>(k);
            // Reject empty boxes (unmapped codepoints).
            if (pc_.x0 == pc_.x1 || pc_.y0 == pc_.y1) {
                // A glyph with zero pixel extent (e.g. SPACE) still
                // contributes xadvance — keep it, but with zero rect.
            }
            GlyphMetrics g{};
            g.codepoint = static_cast<std::uint16_t>(cp);
            g.u0        = static_cast<std::int16_t>(pc_.x0);
            g.v0        = static_cast<std::int16_t>(pc_.y0);
            g.u1        = static_cast<std::int16_t>(pc_.x1);
            g.v1        = static_cast<std::int16_t>(pc_.y1);
            // stb_truetype hands us float xoff/yoff/xadvance; round to
            // integer pixels so the layout is deterministic across
            // platforms (we already avoid subpixel positioning).
            g.xoff      = static_cast<std::int16_t>(pc_.xoff);
            g.yoff      = static_cast<std::int16_t>(pc_.yoff);
            g.xadvance  = static_cast<std::int16_t>(pc_.xadvance);
            out.indexByCp.emplace(cp, static_cast<std::uint32_t>(out.glyphs.size()));
            out.glyphs.push_back(g);
        }
    }

    return out;
}

FontAtlas bakeFontFromFile(std::string_view path, const FontConfig& cfg) {
    std::ifstream in(std::string(path), std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size <= 0) return {};
    in.seekg(0, std::ios::beg);
    std::vector<std::byte> buf(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(buf.data()), size);
    if (!in) return {};
    return bakeFont(buf, cfg);
}

}  // namespace tou2d::ui
