// tou2d_shp_body_test — pins the .SHP body layout.
//
// Body layout (verified empirically across all 9 stock SHPs and
// documented in `examples/tou2d/ShpBody.hpp`):
//
//   file_size = header_bytes + 32 * 3 * frame_w * frame_h
//   body_start = file_size - 32 * 3 * frame_w * frame_h
//   each frame = 3 bytes per pixel, interleaved triplet
//     byte 0 = hull palette index
//     byte 1 = edge-highlight palette index
//     byte 2 = cockpit/detail palette index
//
// This test drives the parser against a synthetic blob — no real
// .SHP file required, so it runs in any CI tree. Cases:
//   * Happy path: 32 frames at 26x26 (PERH-shaped) parse correctly.
//   * Frame indexing returns the right rotation slice.
//   * Pixel indexing returns the right (b0, b1, b2) triplet.
//   * compositeRotation does byte[2] over byte[0] priority correctly.
//   * compositeRotation makes palette index 0 fully transparent.
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
    test_too_small();
    test_zero_dims();
    test_body_overlaps_header();
    EXIT_WITH_RESULT();
}
