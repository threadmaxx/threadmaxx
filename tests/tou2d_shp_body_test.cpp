// tou2d_shp_body_test — pins the .SHP body layout.
//
// Body layout (verified empirically across all 9 stock SHPs and
// documented in `examples/tou2d/ShpBody.hpp`):
//
//   file_size = header_bytes + 32 * 3 * frame_w * frame_h
//   body_start = file_size - 32 * 3 * frame_w * frame_h
//   each frame = 3 bytes per pixel, interleaved triplet
//     byte 0 = hull intensity      (cross-brackets / structural elements)
//     byte 1 = team intensity      (wings / faction-color region)
//     byte 2 = cockpit intensity   (cockpit-sphere / center detail)
//
// This test drives the parser against a synthetic blob — no real
// .SHP file required, so it runs in any CI tree. Cases:
//   * Happy path: 32 frames at 26x26 (PERH-shaped) parse correctly.
//   * Frame indexing returns the right rotation slice.
//   * Pixel indexing returns the right (b0, b1, b2) triplet.
//   * Legacy compositeRotation does byte[2] over byte[0] priority.
//   * Legacy compositeRotation makes palette index 0 fully transparent.
//   * primarySentinel returns the formula's (x, y) for f0..f30 and
//     nullopt for f31.
//   * compositeRotationCentered's frame-31 special case (no shift).
//   * compositeRotationCentered clears the trailer triplet.
//   * compositeRotationCentered blends per-channel intensities.
//   * Too-small data returns false.
//   * Zero width or height returns false.
//   * Body overlapping the header returns false.

#include "Check.hpp"

#include "../examples/tou2d/ShpHeader.hpp"
#include "../examples/tou2d/ShpBody.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace {

/// Build a synthetic body — 32 frames of `w*h` pixels × 3 bytes each.
/// Pixel (x, y) of rotation `r` is set to a deterministic triplet
/// (`r+1`, `r+2`, `r+3`) modulo 256, so each rotation's data is
/// distinguishable.
std::vector<std::uint8_t> makeBodyBlob(std::uint16_t w, std::uint16_t h,
                                       std::size_t leadingPadding) {
    const std::size_t frameSize =
        std::size_t{w} * std::size_t{h} * tou2d::shp::kBodyBytesPerPixel;
    const std::size_t bodySize =
        std::size_t{tou2d::shp::kBodyRotationCount} * frameSize;
    std::vector<std::uint8_t> blob(leadingPadding + bodySize, 0u);

    for (std::uint32_t r = 0; r < tou2d::shp::kBodyRotationCount; ++r) {
        const std::size_t frameBase = leadingPadding + std::size_t{r} * frameSize;
        for (std::size_t i = 0; i < frameSize / 3; ++i) {
            blob[frameBase + i * 3 + 0] = static_cast<std::uint8_t>(r + 1);
            blob[frameBase + i * 3 + 1] = static_cast<std::uint8_t>(r + 2);
            blob[frameBase + i * 3 + 2] = static_cast<std::uint8_t>(r + 3);
        }
    }
    return blob;
}

void test_happy_path() {
    constexpr std::uint16_t W = 26;
    constexpr std::uint16_t H = 26;
    constexpr std::size_t LEADING = 601;  // mimics PERH's header size

    auto blob = makeBodyBlob(W, H, LEADING);

    tou2d::shp::ParsedHeader hdr{};
    hdr.frameWidth   = W;
    hdr.frameHeight  = H;
    hdr.payloadStart = 43;  // mimics PERH parser output

    tou2d::shp::ParsedBody body{};
    CHECK(tou2d::shp::parseBody(blob, hdr, body));
    CHECK_EQ(body.bodyStart, LEADING);
    CHECK_EQ(body.frameWidth, W);
    CHECK_EQ(body.frameHeight, H);
    CHECK_EQ(body.frameSize, std::size_t{W} * H * 3);
    CHECK_EQ(body.body.size(), 32u * std::size_t{W} * H * 3);
}

void test_frame_indexing() {
    constexpr std::uint16_t W = 26;
    constexpr std::uint16_t H = 26;
    auto blob = makeBodyBlob(W, H, /*leading=*/601);

    tou2d::shp::ParsedHeader hdr{};
    hdr.frameWidth   = W;
    hdr.frameHeight  = H;
    hdr.payloadStart = 43;

    tou2d::shp::ParsedBody body{};
    CHECK(tou2d::shp::parseBody(blob, hdr, body));

    // Frame r has every pixel set to (r+1, r+2, r+3).
    for (std::uint32_t r = 0; r < 32; ++r) {
        const auto p = body.pixel(r, 5, 5);
        CHECK_EQ(static_cast<int>(p.hull),    static_cast<int>(r + 1));
        CHECK_EQ(static_cast<int>(p.edge),    static_cast<int>(r + 2));
        CHECK_EQ(static_cast<int>(p.cockpit), static_cast<int>(r + 3));
    }
}

void test_composite_priority() {
    constexpr std::uint16_t W = 4;
    constexpr std::uint16_t H = 4;
    constexpr std::size_t LEADING = 8;
    constexpr std::size_t pixelCount = std::size_t{W} * H;

    std::vector<std::uint8_t> blob(LEADING + std::size_t{W} * H * 3 * 32, 0u);
    // Frame 0 only: row 0 = hull only, row 1 = cockpit only, row 2 = both, row 3 = neither.
    for (std::uint32_t x = 0; x < W; ++x) {
        // row 0: hull=42, cockpit=0 -> visible as 42
        blob[LEADING + (0 * W + x) * 3 + 0] = 42;
        // row 1: hull=0, cockpit=77 -> visible as 77
        blob[LEADING + (1 * W + x) * 3 + 2] = 77;
        // row 2: hull=11, cockpit=22 -> visible as 22 (cockpit wins)
        blob[LEADING + (2 * W + x) * 3 + 0] = 11;
        blob[LEADING + (2 * W + x) * 3 + 2] = 22;
        // row 3: hull=0, cockpit=0 -> transparent
    }

    tou2d::shp::ParsedHeader hdr{};
    hdr.frameWidth   = W;
    hdr.frameHeight  = H;
    hdr.payloadStart = 4;

    tou2d::shp::ParsedBody body{};
    CHECK(tou2d::shp::parseBody(blob, hdr, body));

    // Build a tiny test palette: idx -> (idx, idx, idx).
    std::array<std::uint8_t, 768> pal{};
    for (std::size_t i = 0; i < 256; ++i) {
        pal[i * 3 + 0] = static_cast<std::uint8_t>(i);
        pal[i * 3 + 1] = static_cast<std::uint8_t>(i);
        pal[i * 3 + 2] = static_cast<std::uint8_t>(i);
    }

    std::vector<std::uint8_t> rgba(pixelCount * 4, 0u);
    tou2d::shp::compositeRotation(body, 0, pal, rgba);

    // Row 0: every pixel should be (42, 42, 42, 255).
    for (std::uint32_t x = 0; x < W; ++x) {
        const std::size_t i = (0 * W + x) * 4;
        CHECK_EQ(static_cast<int>(rgba[i + 0]), 42);
        CHECK_EQ(static_cast<int>(rgba[i + 3]), 255);
    }
    // Row 1: every pixel should be (77, 77, 77, 255).
    for (std::uint32_t x = 0; x < W; ++x) {
        const std::size_t i = (1 * W + x) * 4;
        CHECK_EQ(static_cast<int>(rgba[i + 0]), 77);
        CHECK_EQ(static_cast<int>(rgba[i + 3]), 255);
    }
    // Row 2: cockpit (22) wins over hull (11).
    for (std::uint32_t x = 0; x < W; ++x) {
        const std::size_t i = (2 * W + x) * 4;
        CHECK_EQ(static_cast<int>(rgba[i + 0]), 22);
        CHECK_EQ(static_cast<int>(rgba[i + 3]), 255);
    }
    // Row 3: fully transparent.
    for (std::uint32_t x = 0; x < W; ++x) {
        const std::size_t i = (3 * W + x) * 4;
        CHECK_EQ(static_cast<int>(rgba[i + 0]), 0);
        CHECK_EQ(static_cast<int>(rgba[i + 3]), 0);
    }
}

void test_primary_sentinel_formula() {
    // PERH-shaped: 26x26, area 676. Formula: flat = 676 - 6*(31 - N).
    // f0 -> flat 490 -> (22, 18); f31 -> nullopt.
    {
        auto s0 = tou2d::shp::primarySentinel(26, 26, 0);
        CHECK(s0.has_value());
        CHECK_EQ(static_cast<int>(s0->x), 22);
        CHECK_EQ(static_cast<int>(s0->y), 18);
    }
    {
        auto s30 = tou2d::shp::primarySentinel(26, 26, 30);
        CHECK(s30.has_value());
        // flat = 676 - 6 = 670 -> (670 % 26 = 20, 670 / 26 = 25).
        CHECK_EQ(static_cast<int>(s30->x), 20);
        CHECK_EQ(static_cast<int>(s30->y), 25);
    }
    {
        auto s31 = tou2d::shp::primarySentinel(26, 26, 31);
        CHECK(!s31.has_value());
    }
    // FLYY-shaped: 32x32, area 1024. f0 -> 838 -> (6, 26).
    {
        auto s0 = tou2d::shp::primarySentinel(32, 32, 0);
        CHECK(s0.has_value());
        CHECK_EQ(static_cast<int>(s0->x), 6);
        CHECK_EQ(static_cast<int>(s0->y), 26);
    }
    // SPED-shaped: 22x22, area 484. f0 -> 298 -> (12, 13).
    {
        auto s0 = tou2d::shp::primarySentinel(22, 22, 0);
        CHECK(s0.has_value());
        CHECK_EQ(static_cast<int>(s0->x), 12);
        CHECK_EQ(static_cast<int>(s0->y), 13);
    }
}

/// Build a body where frame `targetRotation` has a single pixel set at
/// pre-shift coordinates (px, py) with the supplied triplet. All other
/// pixels of every frame are zero.
std::vector<std::uint8_t>
makeSinglePixelBlob(std::uint16_t w, std::uint16_t h,
                    std::size_t leading,
                    std::uint32_t targetRotation,
                    std::uint32_t px, std::uint32_t py,
                    std::uint8_t b0, std::uint8_t b1, std::uint8_t b2) {
    const std::size_t frameSize =
        std::size_t{w} * std::size_t{h} * tou2d::shp::kBodyBytesPerPixel;
    const std::size_t bodySize =
        std::size_t{tou2d::shp::kBodyRotationCount} * frameSize;
    std::vector<std::uint8_t> blob(leading + bodySize, 0u);
    const std::size_t frameBase = leading + std::size_t{targetRotation} * frameSize;
    const std::size_t o = (std::size_t{py} * w + px) * 3;
    blob[frameBase + o + 0] = b0;
    blob[frameBase + o + 1] = b1;
    blob[frameBase + o + 2] = b2;
    return blob;
}

void test_centered_frame31_no_shift() {
    // Frame 31 is rendered untouched. Place a single hull-only pixel at
    // pre-shift (3, 4) — it should appear at dst (3, 4).
    constexpr std::uint16_t W = 26;
    constexpr std::uint16_t H = 26;
    auto blob = makeSinglePixelBlob(W, H, /*leading=*/8, /*rot=*/31,
                                    /*px=*/3, /*py=*/4,
                                    /*b0=*/200, /*b1=*/0, /*b2=*/0);

    tou2d::shp::ParsedHeader hdr{};
    hdr.frameWidth   = W;
    hdr.frameHeight  = H;
    hdr.payloadStart = 8;

    tou2d::shp::ParsedBody body{};
    CHECK(tou2d::shp::parseBody(blob, hdr, body));

    tou2d::shp::ShipColors colors{};
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(W) * H * 4, 0u);
    tou2d::shp::compositeRotationCentered(body, 31, colors, rgba);

    // Destination (3, 4) should be opaque hull-color * 200/255.
    const std::size_t di = (4 * W + 3) * 4;
    CHECK_EQ(static_cast<int>(rgba[di + 3]), 255);
    // Hull-color blue channel is 180; 180 * 200/255 with round-to-nearest = 141.
    CHECK_EQ(static_cast<int>(rgba[di + 2]), 141);
    // Every other pixel must be transparent.
    int opaqueCount = 0;
    for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(W) * H; ++i) {
        if (rgba[i * 4 + 3] != 0) ++opaqueCount;
    }
    CHECK_EQ(opaqueCount, 1);
}

void test_centered_trailer_suppressed() {
    // Frame 0: place the 3-pixel trailer at the canonical sentinel position,
    // and a regular pixel adjacent to it. After centering: the trailer pixels
    // should appear as fully transparent at post-shift row H-1 cols 0/2/4.
    constexpr std::uint16_t W = 26;
    constexpr std::uint16_t H = 26;
    const auto s = tou2d::shp::primarySentinel(W, H, 0);
    CHECK(s.has_value());
    const std::uint32_t sx = s->x;
    const std::uint32_t sy = s->y;

    const std::size_t leading  = 8;
    const std::size_t frameSize =
        std::size_t{W} * std::size_t{H} * tou2d::shp::kBodyBytesPerPixel;
    const std::size_t bodySize =
        std::size_t{tou2d::shp::kBodyRotationCount} * frameSize;
    std::vector<std::uint8_t> blob(leading + bodySize, 0u);
    const std::size_t fb0 = leading;
    auto put = [&](std::uint32_t x, std::uint32_t y,
                   std::uint8_t b0, std::uint8_t b1, std::uint8_t b2) {
        const std::size_t o = (std::size_t{y} * W + x) * 3;
        blob[fb0 + o + 0] = b0;
        blob[fb0 + o + 1] = b1;
        blob[fb0 + o + 2] = b2;
    };
    // Canonical trailer:
    //   sentinel:   (0, 0, 2) at (sx,   sy)
    //   secondary:  (0, 24, 0) at (sx+2, sy) — wrap not needed for PERH f0
    //   tertiary:   (W, 0, W) at (sx+4, sy)
    put(sx,     sy, 0, 0,  2);
    put(sx + 2, sy, 0, 24, 0);
    put(sx + 4, sy, W, 0,  W);
    // A regular pixel right next to the sentinel (still pre-shift).
    put(sx, sy + 1, 0, 0, 200);

    tou2d::shp::ParsedHeader hdr{};
    hdr.frameWidth   = W;
    hdr.frameHeight  = H;
    hdr.payloadStart = leading;

    tou2d::shp::ParsedBody body{};
    CHECK(tou2d::shp::parseBody(blob, hdr, body));

    tou2d::shp::ShipColors colors{};
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(W) * H * 4, 0u);
    tou2d::shp::compositeRotationCentered(body, 0, colors, rgba);

    auto alphaAt = [&](std::uint32_t x, std::uint32_t y) {
        return rgba[(std::size_t{y} * W + x) * 4 + 3];
    };
    // Trailer pixels at post-shift (0/2/4, H-1) MUST be transparent.
    CHECK_EQ(static_cast<int>(alphaAt(0, H - 1)), 0);
    CHECK_EQ(static_cast<int>(alphaAt(2, H - 1)), 0);
    CHECK_EQ(static_cast<int>(alphaAt(4, H - 1)), 0);
}

void test_centered_blend_color_model() {
    // Build a body where frame 31 (untouched) has one pixel with each
    // channel set in turn, and verify the blend produces the expected
    // additive value.
    constexpr std::uint16_t W = 4;
    constexpr std::uint16_t H = 4;
    const std::size_t leading = 0;
    const std::size_t frameSize =
        std::size_t{W} * std::size_t{H} * tou2d::shp::kBodyBytesPerPixel;
    std::vector<std::uint8_t> blob(
        leading + std::size_t{tou2d::shp::kBodyRotationCount} * frameSize, 0u);
    const std::size_t fb31 = leading + 31u * frameSize;
    auto put = [&](std::uint32_t x, std::uint32_t y,
                   std::uint8_t b0, std::uint8_t b1, std::uint8_t b2) {
        const std::size_t o = (std::size_t{y} * W + x) * 3;
        blob[fb31 + o + 0] = b0;
        blob[fb31 + o + 1] = b1;
        blob[fb31 + o + 2] = b2;
    };
    put(0, 0, 255,   0,   0);  // pure hull
    put(1, 0,   0, 255,   0);  // pure team
    put(2, 0,   0,   0, 255);  // pure cockpit
    put(3, 0, 255, 255, 255);  // all three at max -> saturated white

    tou2d::shp::ParsedHeader hdr{};
    hdr.frameWidth   = W;
    hdr.frameHeight  = H;
    hdr.payloadStart = 0;

    tou2d::shp::ParsedBody body{};
    CHECK(tou2d::shp::parseBody(blob, hdr, body));

    tou2d::shp::ShipColors colors{
        /*hull=*/   100, 100, 100,
        /*team=*/    50,  60,  70,
        /*cockpit=*/ 80,  90, 100,
    };
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(W) * H * 4, 0u);
    tou2d::shp::compositeRotationCentered(body, 31, colors, rgba);

    auto at = [&](std::uint32_t x, std::uint32_t y) {
        const std::size_t i = (std::size_t{y} * W + x) * 4;
        return std::array<int, 4>{
            static_cast<int>(rgba[i + 0]), static_cast<int>(rgba[i + 1]),
            static_cast<int>(rgba[i + 2]), static_cast<int>(rgba[i + 3])
        };
    };
    auto px00 = at(0, 0);
    CHECK_EQ(px00[0], 100); CHECK_EQ(px00[1], 100); CHECK_EQ(px00[2], 100); CHECK_EQ(px00[3], 255);
    auto px10 = at(1, 0);
    CHECK_EQ(px10[0],  50); CHECK_EQ(px10[1],  60); CHECK_EQ(px10[2],  70); CHECK_EQ(px10[3], 255);
    auto px20 = at(2, 0);
    CHECK_EQ(px20[0],  80); CHECK_EQ(px20[1],  90); CHECK_EQ(px20[2], 100); CHECK_EQ(px20[3], 255);
    auto px30 = at(3, 0);
    // 100 + 50 + 80 = 230; 100 + 60 + 90 = 250; 100 + 70 + 100 = 270 -> clamp 255.
    CHECK_EQ(px30[0], 230); CHECK_EQ(px30[1], 250); CHECK_EQ(px30[2], 255); CHECK_EQ(px30[3], 255);
    // Untouched pixel must be fully transparent.
    auto px01 = at(0, 1);
    CHECK_EQ(px01[3], 0);
}

void test_centered_primary_shift_alignment() {
    // Build a body where frame 0 has its 3-pixel trailer + one extra pixel
    // adjacent to the sentinel. After centering, the adjacent pixel should
    // land at a known post-shift coordinate, and the trailer at row H-1.
    constexpr std::uint16_t W = 26;
    constexpr std::uint16_t H = 26;
    const auto s = tou2d::shp::primarySentinel(W, H, 0);
    CHECK(s.has_value());
    const std::uint32_t sx = s->x;
    const std::uint32_t sy = s->y;

    const std::size_t leading = 0;
    const std::size_t frameSize =
        std::size_t{W} * std::size_t{H} * tou2d::shp::kBodyBytesPerPixel;
    std::vector<std::uint8_t> blob(
        std::size_t{tou2d::shp::kBodyRotationCount} * frameSize, 0u);
    const std::size_t fb0 = leading;
    auto put = [&](std::uint32_t x, std::uint32_t y,
                   std::uint8_t b0, std::uint8_t b1, std::uint8_t b2) {
        const std::size_t o = (std::size_t{y} * W + x) * 3;
        blob[fb0 + o + 0] = b0;
        blob[fb0 + o + 1] = b1;
        blob[fb0 + o + 2] = b2;
    };
    // Trailer.
    put(sx,     sy, 0, 0,  2);
    put(sx + 2, sy, 0, 24, 0);
    put(sx + 4, sy, W, 0,  W);
    // Hull-only pixel one row below the sentinel in pre-shift coords.
    // Under the (sx, sy+1) primary shift this lands at post-shift (0, 0),
    // which is in the TOP half (top_height = H-sy-1 > 0). The secondary
    // +6 column shift means we look for it at post-shift (W-6, 0).
    put(sx, sy + 1, 222, 0, 0);

    tou2d::shp::ParsedHeader hdr{};
    hdr.frameWidth   = W;
    hdr.frameHeight  = H;
    hdr.payloadStart = leading;

    tou2d::shp::ParsedBody body{};
    CHECK(tou2d::shp::parseBody(blob, hdr, body));

    tou2d::shp::ShipColors colors{};
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(W) * H * 4, 0u);
    tou2d::shp::compositeRotationCentered(body, 0, colors, rgba);

    // Without the secondary shift, the pixel would land at post-primary
    // (0, 0). With +6 secondary, the destination column = (0 - 6 + W) % W
    // = W - 6 = 20. So the visible pixel is at (W - tou2d::shp::kBodyTopHalfShift, 0).
    const std::uint32_t expectedX = W - tou2d::shp::kBodyTopHalfShift;
    auto alphaAt = [&](std::uint32_t x, std::uint32_t y) {
        return rgba[(std::size_t{y} * W + x) * 4 + 3];
    };
    CHECK_EQ(static_cast<int>(alphaAt(expectedX, 0)), 255);
    // And (0, 0) should be transparent now.
    CHECK_EQ(static_cast<int>(alphaAt(0, 0)), 0);
}

void test_too_small() {
    std::vector<std::uint8_t> tiny(100, 0u);
    tou2d::shp::ParsedHeader hdr{};
    hdr.frameWidth   = 26;
    hdr.frameHeight  = 26;
    hdr.payloadStart = 43;

    tou2d::shp::ParsedBody body{};
    CHECK(!tou2d::shp::parseBody(tiny, hdr, body));
}

void test_zero_dims() {
    std::vector<std::uint8_t> blob(100000, 0u);
    tou2d::shp::ParsedBody body{};

    tou2d::shp::ParsedHeader hdr{};
    hdr.frameHeight  = 26;
    hdr.payloadStart = 43;
    hdr.frameWidth   = 0;  // zero w
    CHECK(!tou2d::shp::parseBody(blob, hdr, body));

    hdr.frameWidth  = 26;
    hdr.frameHeight = 0;   // zero h
    CHECK(!tou2d::shp::parseBody(blob, hdr, body));
}

void test_body_overlaps_header() {
    // payloadStart claims everything past byte 100 is body, but with
    // 26x26 the body would need to start at file_size - 64896 — if
    // that comes BEFORE payloadStart, the file is malformed.
    constexpr std::uint16_t W = 26;
    constexpr std::uint16_t H = 26;
    const std::size_t bodySize = 32u * W * H * 3;
    std::vector<std::uint8_t> blob(bodySize + 10, 0u);  // only 10 bytes of "header"

    tou2d::shp::ParsedHeader hdr{};
    hdr.frameWidth   = W;
    hdr.frameHeight  = H;
    hdr.payloadStart = 50;  // claims header occupies 50 bytes, but file only has 10

    tou2d::shp::ParsedBody body{};
    CHECK(!tou2d::shp::parseBody(blob, hdr, body));
}

} // namespace

int main() {
    test_happy_path();
    test_frame_indexing();
    test_composite_priority();
    test_primary_sentinel_formula();
    test_centered_frame31_no_shift();
    test_centered_trailer_suppressed();
    test_centered_blend_color_model();
    test_centered_primary_shift_alignment();
    test_too_small();
    test_zero_dims();
    test_body_overlaps_header();
    EXIT_WITH_RESULT();
}
