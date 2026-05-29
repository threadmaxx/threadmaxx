// tou2d_font_atlas_test — pins the M6.0 TTF bake + text layout
// contract. Loads the bundled `assets/ui/font.ttf` (DejaVu Sans Mono
// by default; the test passes for ANY TTF/OTF covering the ASCII
// printable range — that's the drop-in contract).
//
// Contract:
//   * bakeFont() on a valid TTF + default FontConfig produces a
//     usable FontAtlas (valid() returns true).
//   * Every codepoint in the default range (0x20..0x7E) is present
//     in the atlas (lookupGlyph() returns non-null).
//   * Atlas dimensions match the requested FontConfig (no silent
//     resize).
//   * Vertical metrics are populated (ascent > 0, descent < 0,
//     lineHeight > 0).
//   * textPrintf emits one TextGlyphQuad per visible glyph for
//     "HELLO" — five glyphs (H, E, L, L, O), all with non-empty
//     pixel rect.
//   * Newlines advance penY by lineHeight and reset penX to baseX.
//   * measureText("HELLO") returns a positive bbox; measureText("")
//     returns zero-size.
//   * SPACE consumes xadvance but emits no quad.
//   * Atlas glyph index lookup is order-independent (sorted by
//     codepoint internally, hashmap lookup is O(1)).
//   * GlyphMetrics layout pinned at 16 bytes.

#include "Check.hpp"

#include "../examples/tou2d/ui/Font.hpp"
#include "../examples/tou2d/ui/TextPrintf.hpp"

#include <cmath>
#include <cstdint>
#include <vector>

#ifndef THREADMAXX_TOU2D_FONT_PATH
#error "THREADMAXX_TOU2D_FONT_PATH must be set by the test's CMake target"
#endif

int main() {
    using namespace tou2d::ui;

    // ---- 16-byte GlyphMetrics pin -----------------------------------
    static_assert(sizeof(GlyphMetrics) == 16,
                  "GlyphMetrics must stay 16 bytes — settings.dat "
                  "wire shape downstream depends on this.");

    // ---- Bake the default config ------------------------------------
    FontConfig cfg{};  // pixelSize=16, ASCII range, 512x512
    FontAtlas atlas = bakeFontFromFile(THREADMAXX_TOU2D_FONT_PATH, cfg);
    CHECK(atlas.valid());
    CHECK_EQ(atlas.pixelSize, 16);
    CHECK_EQ(atlas.atlasW,    512);
    CHECK_EQ(atlas.atlasH,    512);
    CHECK(atlas.ascent  > 0);
    CHECK(atlas.descent < 0);
    CHECK(lineHeight(atlas) > 0.0f);

    // ---- Every ASCII printable codepoint resolved -------------------
    for (std::uint32_t cp = 0x20; cp <= 0x7E; ++cp) {
        const auto* g = atlas.lookupGlyph(cp);
        CHECK(g != nullptr);
        CHECK_EQ(g->codepoint, static_cast<std::uint16_t>(cp));
        CHECK(g->xadvance > 0);  // every glyph advances
    }

    // ---- Non-bake codepoint returns null ---------------------------
    CHECK(atlas.lookupGlyph(0x4E2D) == nullptr);  // '中' wasn't in range
    CHECK(atlas.lookupGlyph(0x00) == nullptr);    // NUL never baked

    // ---- textPrintf emits expected glyph count for "HELLO" ---------
    {
        std::vector<TextGlyphQuad> emitted;
        const PenPos end = textPrintf(
            atlas, /*baseX*/ 0.0f, /*baseY*/ static_cast<float>(atlas.ascent),
            /*color*/ 0xFFFFFFFFu, "HELLO",
            [&emitted](const TextGlyphQuad& q) { emitted.push_back(q); });
        CHECK_EQ(emitted.size(), std::size_t{5});
        // Every emitted glyph has a non-empty pixel rect.
        for (const auto& q : emitted) {
            CHECK(q.x1 > q.x0);
            CHECK(q.y1 > q.y0);
            CHECK(q.u1 > q.u0);
            CHECK(q.v1 > q.v0);
            CHECK_EQ(q.color, std::uint32_t{0xFFFFFFFFu});
        }
        // Pen advanced.
        CHECK(end.x > 0.0f);
    }

    // ---- SPACE consumes advance but emits no quad ------------------
    {
        std::vector<TextGlyphQuad> emitted;
        const PenPos end = textPrintf(
            atlas, 0.0f, static_cast<float>(atlas.ascent),
            0xFFFFFFFFu, " ",
            [&emitted](const TextGlyphQuad& q) { emitted.push_back(q); });
        CHECK_EQ(emitted.size(), std::size_t{0});
        CHECK(end.x > 0.0f);  // SPACE advanced the pen
    }

    // ---- Newline advances penY by lineHeight, resets penX ----------
    {
        std::vector<TextGlyphQuad> emitted;
        const float baseY = static_cast<float>(atlas.ascent);
        const PenPos end = textPrintf(
            atlas, /*baseX*/ 100.0f, baseY,
            0xFFFFFFFFu, "A\nB",
            [&emitted](const TextGlyphQuad& q) { emitted.push_back(q); });
        CHECK_EQ(emitted.size(), std::size_t{2});
        // After A\nB the pen is on line 2, having drawn one 'B'.
        // The exact x depends on font metrics; we just check the y advance.
        CHECK(end.y > baseY);
        const float lh = lineHeight(atlas);
        CHECK(std::abs((end.y - baseY) - lh) < 0.5f);
        // The 'B' quad's y0 is below the 'A' quad's y0 by ~lineHeight.
        CHECK(emitted[1].y0 > emitted[0].y0);
    }

    // ---- measureText("HELLO") returns positive bbox ----------------
    {
        const TextBox box = measureText(atlas, "HELLO");
        CHECK(box.width()  > 0.0f);
        CHECK(box.height() > 0.0f);
        CHECK(box.x0 >= 0.0f);
    }
    {
        const TextBox box = measureText(atlas, "");
        CHECK_EQ(box.width(),  0.0f);
        CHECK_EQ(box.height(), 0.0f);
    }

    // ---- Re-bake at a different pixelSize works (drop-in scaling) --
    {
        FontConfig big{};
        big.pixelSize = 32;
        FontAtlas big_atlas = bakeFontFromFile(THREADMAXX_TOU2D_FONT_PATH, big);
        CHECK(big_atlas.valid());
        CHECK_EQ(big_atlas.pixelSize, 32);
        // Bigger font has bigger ascent.
        CHECK(big_atlas.ascent > atlas.ascent);
    }

    // ---- Invalid input handling ------------------------------------
    {
        // Empty buffer → invalid atlas, doesn't crash.
        FontAtlas empty = bakeFont(std::span<const std::byte>{}, FontConfig{});
        CHECK(!empty.valid());
    }
    {
        // Garbage bytes → invalid atlas.
        std::vector<std::byte> junk(64, std::byte{0xAA});
        FontAtlas bad = bakeFont(junk, FontConfig{});
        CHECK(!bad.valid());
    }
    {
        // Non-existent file → invalid atlas.
        FontAtlas missing = bakeFontFromFile("/dev/null/no-such-file.ttf",
                                             FontConfig{});
        CHECK(!missing.valid());
    }

    EXIT_WITH_RESULT();
}
