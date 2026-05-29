// tou2d_ui_overlay_bitmap_test — M6.0b CPU compositor contract.
//
// Pins the UiOverlayBitmap public behavior. The renderer consumes
// it via setUiOverlayFromRgba / updateUiOverlayRegion, so the bytes
// the bitmap writes ARE what hits the GPU texture — anything tested
// here is observable end-to-end.
//
// Contract:
//   * resize() allocates the right pixel count and marks the entire
//     bitmap dirty (so the first upload pushes the whole frame).
//   * clear(rgba) fills every pixel and dirties the whole bitmap.
//   * clearRect() clips to bounds and only dirties the touched rect.
//   * blitGlyphR8Alpha() composites an R8 alpha source onto the
//     RGBA8 backbuffer at the requested color, with straight-alpha
//     "over" semantics.
//   * drawText() runs textPrintf and blits per visible glyph; the
//     dirty rect spans the rendered glyphs.
//   * consumeDirty() returns AND clears the dirty rect.
//   * extractRegion() copies a rect-shaped slice into a caller
//     buffer in RGBA8 row-major order.

#include "Check.hpp"

#include "../examples/tou2d/ui/Font.hpp"
#include "../examples/tou2d/ui/UiOverlayBitmap.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

#ifndef THREADMAXX_TOU2D_FONT_PATH
#error "THREADMAXX_TOU2D_FONT_PATH must be set by the test's CMake target"
#endif

namespace {

constexpr std::uint32_t kWhite        = 0xFFFFFFFFu;  // 0xAABBGGRR
constexpr std::uint32_t kRed          = 0xFF0000FFu;  // A=FF, B=00, G=00, R=FF
constexpr std::uint32_t kTransparent  = 0u;

}  // namespace

int main() {
    using tou2d::ui::UiOverlayBitmap;
    using tou2d::ui::DirtyRect;

    // ---- resize() ---------------------------------------------------
    {
        UiOverlayBitmap bmp;
        bmp.resize(64, 32);
        CHECK_EQ(bmp.width(),  std::uint32_t{64});
        CHECK_EQ(bmp.height(), std::uint32_t{32});
        CHECK_EQ(bmp.pixels().size(), std::size_t{64u * 32u});
        // Whole bitmap dirty.
        const auto d = bmp.dirty();
        CHECK_EQ(d.x, std::uint32_t{0});
        CHECK_EQ(d.y, std::uint32_t{0});
        CHECK_EQ(d.w, std::uint32_t{64});
        CHECK_EQ(d.h, std::uint32_t{32});
    }

    // ---- clear() ----------------------------------------------------
    {
        UiOverlayBitmap bmp;
        bmp.resize(8, 4);
        (void)bmp.consumeDirty();
        bmp.clear(kRed);
        for (auto px : bmp.pixels()) {
            CHECK_EQ(px, kRed);
        }
        const auto d = bmp.dirty();
        CHECK_EQ(d.w, std::uint32_t{8});
        CHECK_EQ(d.h, std::uint32_t{4});
    }

    // ---- clearRect() clip + dirty bounds ----------------------------
    {
        UiOverlayBitmap bmp;
        bmp.resize(16, 16);
        bmp.clear(kTransparent);
        (void)bmp.consumeDirty();

        // Inside paint: (4, 4, 4, 4) = a 4x4 red square at (4,4).
        bmp.clearRect(4, 4, 4, 4, kRed);
        const auto d = bmp.dirty();
        CHECK_EQ(d.x, std::uint32_t{4});
        CHECK_EQ(d.y, std::uint32_t{4});
        CHECK_EQ(d.w, std::uint32_t{4});
        CHECK_EQ(d.h, std::uint32_t{4});
        // Corner pixels of the painted rect:
        CHECK_EQ(bmp.pixels()[ 4u * 16u + 4u], kRed);
        CHECK_EQ(bmp.pixels()[ 7u * 16u + 7u], kRed);
        // Just outside the rect:
        CHECK_EQ(bmp.pixels()[ 3u * 16u + 4u], kTransparent);
        CHECK_EQ(bmp.pixels()[ 4u * 16u + 3u], kTransparent);
        CHECK_EQ(bmp.pixels()[ 8u * 16u + 4u], kTransparent);
        CHECK_EQ(bmp.pixels()[ 4u * 16u + 8u], kTransparent);

        // OOB paint is a no-op (no crash, no dirty extension below).
        (void)bmp.consumeDirty();
        bmp.clearRect(100, 100, 50, 50, kRed);
        CHECK(bmp.dirty().empty());
        bmp.clearRect(-10, -10, 5, 5, kRed);
        CHECK(bmp.dirty().empty());
    }

    // ---- blitGlyphR8Alpha: single solid pixel ----------------------
    {
        UiOverlayBitmap bmp;
        bmp.resize(4, 4);
        bmp.clear(kTransparent);
        (void)bmp.consumeDirty();

        // A 1x1 source with full alpha at (0,0).
        std::byte src[1] = { std::byte{0xFF} };
        bmp.blitGlyphR8Alpha(src, /*srcW=*/1, /*srcH=*/1, /*srcStride=*/1,
                             /*dstX=*/2, /*dstY=*/2,
                             /*tintRgba=*/kWhite);
        CHECK_EQ(bmp.pixels()[ 2u * 4u + 2u], kWhite);
        // Neighbors untouched.
        CHECK_EQ(bmp.pixels()[ 1u * 4u + 2u], kTransparent);
        CHECK_EQ(bmp.pixels()[ 2u * 4u + 1u], kTransparent);
        // Dirty rect spans just the touched pixel.
        const auto d = bmp.dirty();
        CHECK_EQ(d.x, std::uint32_t{2});
        CHECK_EQ(d.y, std::uint32_t{2});
        CHECK_EQ(d.w, std::uint32_t{1});
        CHECK_EQ(d.h, std::uint32_t{1});
    }

    // ---- blitGlyphR8Alpha: half alpha blends over kRed -------------
    {
        UiOverlayBitmap bmp;
        bmp.resize(2, 1);
        bmp.clear(kRed);  // backbuffer red (opaque)
        (void)bmp.consumeDirty();

        std::byte src[2] = { std::byte{0x80}, std::byte{0xFF} };
        bmp.blitGlyphR8Alpha(src, 2, 1, 2, 0, 0, kWhite);
        // Pixel 0: red OVER 50%-alpha white — should be pinkish but
        // non-trivial; check both that it changed AND its alpha is
        // 0xFF (we composite onto an opaque background → outA = 0xFF).
        const std::uint32_t p0 = bmp.pixels()[0];
        CHECK(p0 != kRed);
        CHECK_EQ((p0 >> 24) & 0xFFu, std::uint32_t{0xFFu});
        // Pixel 1: full-alpha white over red → exactly kWhite.
        CHECK_EQ(bmp.pixels()[1], kWhite);
    }

    // ---- blitGlyphR8Alpha: OOB clip silently ----------------------
    {
        UiOverlayBitmap bmp;
        bmp.resize(4, 4);
        bmp.clear(kTransparent);
        (void)bmp.consumeDirty();

        std::byte src[4] = {std::byte{0xFF}, std::byte{0xFF},
                            std::byte{0xFF}, std::byte{0xFF}};
        // Source 4x1 at dstX=2 → 2 pixels are inside, 2 are OOB.
        bmp.blitGlyphR8Alpha(src, 4, 1, 4, 2, 0, kWhite);
        CHECK_EQ(bmp.pixels()[0], kTransparent);
        CHECK_EQ(bmp.pixels()[1], kTransparent);
        CHECK_EQ(bmp.pixels()[2], kWhite);
        CHECK_EQ(bmp.pixels()[3], kWhite);
        const auto d = bmp.dirty();
        CHECK_EQ(d.x, std::uint32_t{2});
        CHECK_EQ(d.y, std::uint32_t{0});
        CHECK_EQ(d.w, std::uint32_t{2});
        CHECK_EQ(d.h, std::uint32_t{1});
    }

    // ---- drawText paints something into the bitmap ----------------
    {
        UiOverlayBitmap bmp;
        bmp.resize(256, 64);
        bmp.clear(kTransparent);
        (void)bmp.consumeDirty();

        tou2d::ui::FontConfig cfg{};
        tou2d::ui::FontAtlas atlas =
            tou2d::ui::bakeFontFromFile(THREADMAXX_TOU2D_FONT_PATH, cfg);
        CHECK(atlas.valid());

        const auto pen = bmp.drawText(atlas,
                                      /*baseX=*/ 4.0f,
                                      /*baseY=*/ static_cast<float>(atlas.ascent),
                                      kWhite, "HELLO");
        CHECK(pen.x > 4.0f);                    // pen advanced
        CHECK(!bmp.dirty().empty());            // SOMETHING got painted
        // At least one pixel is non-transparent.
        bool anyOpaque = false;
        for (auto px : bmp.pixels()) {
            if ((px >> 24) & 0xFFu) { anyOpaque = true; break; }
        }
        CHECK(anyOpaque);
    }

    // ---- consumeDirty returns + clears ----------------------------
    {
        UiOverlayBitmap bmp;
        bmp.resize(8, 8);
        bmp.clear(kTransparent);

        const auto d1 = bmp.consumeDirty();
        CHECK_EQ(d1.w, std::uint32_t{8});
        CHECK_EQ(d1.h, std::uint32_t{8});
        const auto d2 = bmp.consumeDirty();
        CHECK(d2.empty());
    }

    // ---- extractRegion: row-major copy ----------------------------
    {
        UiOverlayBitmap bmp;
        bmp.resize(4, 4);
        bmp.clearRect(0, 0, 4, 4, kTransparent);
        bmp.clearRect(1, 1, 2, 2, kRed);
        (void)bmp.consumeDirty();

        std::vector<std::uint8_t> scratch(2u * 2u * 4u);
        const bool ok = bmp.extractRegion(
            DirtyRect{1, 1, 2, 2},
            std::span<std::uint8_t>(scratch.data(), scratch.size()));
        CHECK(ok);
        // Each 4-byte word is the packed kRed (little-endian).
        for (std::size_t i = 0; i < 4; ++i) {
            std::uint32_t got = 0;
            std::memcpy(&got, scratch.data() + i * 4u, 4u);
            CHECK_EQ(got, kRed);
        }
        // Oversize / empty rect rejection.
        std::vector<std::uint8_t> tiny(3);
        CHECK(!bmp.extractRegion(DirtyRect{1, 1, 2, 2},
                                 std::span<std::uint8_t>(tiny.data(), tiny.size())));
        CHECK(!bmp.extractRegion(DirtyRect{},
                                 std::span<std::uint8_t>(scratch.data(), scratch.size())));
        CHECK(!bmp.extractRegion(DirtyRect{3, 3, 4, 4},  // OOB right/bottom
                                 std::span<std::uint8_t>(scratch.data(), scratch.size())));
    }

    EXIT_WITH_RESULT();
}
