// tou2d_shp_import_test — pins the .SHP header parser.
//
// The Tier-2 SHP importer (`examples/tou2d/tou2d_import_shp.cpp`)
// decodes a stable prefix of a TOU ship file: a 0x00 padding byte, a
// NUL-terminated ASCII display name, a 3-byte stat triplet, a 4-byte
// stat-extra block (the 4th byte = max-HP across all 9 stock ships),
// and a search-located W/H/rotation anchor `WW 00 HH 00 18 20`.
// Everything past the anchor is opaque body. The test drives the
// parser directly against synthetic byte streams — no real .SHP file
// required, so it runs in any CI tree.
//
// Cases covered:
//   * Happy path: matches the byte layout observed in PERH.SHP
//     ("Butterfly", stat triplet 02 02 01, stat-extra 6e 55 14 14,
//     anchor at the right offset with W=H=26, rot=24).
//   * "Destroyer" — second stock-file shape (W=H=30).
//   * Empty input.
//   * Too-small input (< 12 bytes).
//   * Wrong leading byte (anything non-zero).
//   * Name length zero (NUL immediately at offset 1).
//   * Non-printable byte inside the name.
//   * No NUL terminator within the 64-byte name cap.
//   * Truncated stat block (NUL hit but < 7 bytes left).
//   * Missing W/H/rot anchor.
//
// Independent of the engine — links against threadmaxx::threadmaxx
// purely so the CMake glue stays uniform with the rest of the test
// suite; nothing inside the engine is touched.

#include "Check.hpp"

// The parser is header-only so a test can include it without dragging
// in the importer's CLI front-end. Path is relative to repo root via
// the test's `target_include_directories`.
#include "../examples/tou2d/ShpHeader.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

/// Build a synthetic SHP-shaped blob with:
///   * 0x00 pad + NUL-terminated `name`
///   * `(s0, s1, s2)` stat triplet
///   * `extra[0..3]` stat-extra block (last byte = max-HP)
///   * `markerBytes` of filler (mimics the per-ship variable-length
///     marker region we don't decode)
///   * `width`, `height` as LE-u16 + 0x18 rotation byte + 0x20 invariant
///   * `bodyBytes` of 0xAA filler trailing
std::vector<std::uint8_t> makeBlob(const char* name,
                                   std::uint8_t s0,
                                   std::uint8_t s1,
                                   std::uint8_t s2,
                                   std::array<std::uint8_t, 4> extra,
                                   std::size_t markerBytes,
                                   std::uint16_t width,
                                   std::uint16_t height,
                                   std::size_t bodyBytes = 16) {
    std::vector<std::uint8_t> b;
    b.push_back(0x00);
    for (const char* p = name; *p; ++p) {
        b.push_back(static_cast<std::uint8_t>(*p));
    }
    b.push_back(0x00);   // name terminator
    b.push_back(s0);
    b.push_back(s1);
    b.push_back(s2);
    for (auto e : extra) b.push_back(e);
    // Marker region — fill with arbitrary non-anchor bytes (use a
    // value that can't possibly look like the start of `WW 00 HH 00
    // 18 20` so a too-greedy parser would fail the test).
    for (std::size_t i = 0; i < markerBytes; ++i) b.push_back(0x42);
    // Anchor: WW 00 HH 00 18 20
    b.push_back(static_cast<std::uint8_t>(width  & 0xFF));
    b.push_back(static_cast<std::uint8_t>(width  >> 8));
    b.push_back(static_cast<std::uint8_t>(height & 0xFF));
    b.push_back(static_cast<std::uint8_t>(height >> 8));
    b.push_back(0x18);
    b.push_back(0x20);
    for (std::size_t i = 0; i < bodyBytes; ++i) b.push_back(0xAA);
    return b;
}

} // namespace

int main() {
    using tou2d::shp::ParsedHeader;
    using tou2d::shp::parseHeader;

    // ---- Happy path: PERH.SHP-shaped input ----------------------------
    {
        // Matches the leading bytes of TOU/ships/PERH.SHP (verified by
        // hex inspection): stat triplet 02 02 01, stat-extra 6e 55 14 14,
        // marker length 11 bytes (00 01 02 01 05 04 64 00 00 02 00),
        // W=H=26, rot=24.
        const auto blob = makeBlob("Butterfly", 0x02, 0x02, 0x01,
                                   {0x6e, 0x55, 0x14, 0x14}, 11, 26, 26);
        ParsedHeader h;
        CHECK(parseHeader(blob, h));
        CHECK_EQ(h.displayName, std::string{"Butterfly"});
        CHECK_EQ(h.statTriplet[0], std::uint8_t{0x02});
        CHECK_EQ(h.statTriplet[1], std::uint8_t{0x02});
        CHECK_EQ(h.statTriplet[2], std::uint8_t{0x01});
        CHECK_EQ(h.statExtra[0],   std::uint8_t{0x6e});
        CHECK_EQ(h.statExtra[3],   std::uint8_t{0x14});
        CHECK_EQ(h.maxHp,          std::uint8_t{0x14});
        CHECK_EQ(h.frameWidth,     std::uint16_t{26});
        CHECK_EQ(h.frameHeight,    std::uint16_t{26});
        CHECK_EQ(h.rotationCount,  std::uint8_t{24});
        // Anchor at 1 (pad) + 9 (name) + 1 (NUL) + 3 (stat) + 4 (extra) + 11 (marker) = 29
        // payloadStart = anchor + 6 = 35
        CHECK_EQ(h.anchorOffset,   std::size_t{29});
        CHECK_EQ(h.payloadStart,   std::size_t{35});
    }

    // ---- Stock-shape sanity: Destroyer (W=H=30, max-HP = 0x32) --------
    {
        const auto blob = makeBlob("Destroyer", 0x06, 0x02, 0x06,
                                   {0x23, 0x23, 0x25, 0x32}, 11, 30, 30);
        ParsedHeader h;
        CHECK(parseHeader(blob, h));
        CHECK_EQ(h.displayName, std::string{"Destroyer"});
        CHECK_EQ(h.maxHp,         std::uint8_t{0x32});
        CHECK_EQ(h.frameWidth,    std::uint16_t{30});
        CHECK_EQ(h.frameHeight,   std::uint16_t{30});
        CHECK_EQ(h.rotationCount, std::uint8_t{24});
    }

    // ---- Stock-shape sanity: Bee (W=H=28) ----------------------------
    {
        const auto blob = makeBlob("B2 Stealth fighter", 0x0a, 0x01, 0x04,
                                   {0x4b, 0x41, 0x17, 0x32}, 12, 28, 28);
        ParsedHeader h;
        CHECK(parseHeader(blob, h));
        CHECK_EQ(h.frameWidth,    std::uint16_t{28});
        CHECK_EQ(h.frameHeight,   std::uint16_t{28});
        CHECK_EQ(h.rotationCount, std::uint8_t{24});
        CHECK_EQ(h.maxHp,         std::uint8_t{0x32});
    }

    // ---- Empty input --------------------------------------------------
    {
        const std::vector<std::uint8_t> empty;
        ParsedHeader h;
        CHECK(!parseHeader(empty, h));
    }

    // ---- Too-small input (< 12 bytes) --------------------------------
    {
        const std::vector<std::uint8_t> tiny{0x00, 'A', 0x00};
        ParsedHeader h;
        CHECK(!parseHeader(tiny, h));
    }

    // ---- Wrong leading byte ------------------------------------------
    {
        auto blob = makeBlob("Anything", 1, 2, 3,
                             {0, 0, 0, 0x32}, 6, 24, 24);
        blob[0] = 0xFF;
        ParsedHeader h;
        CHECK(!parseHeader(blob, h));
    }

    // ---- Empty name (NUL at offset 1) ---------------------------------
    {
        std::vector<std::uint8_t> blob(64, 0);
        blob[0] = 0x00;
        blob[1] = 0x00;  // immediate NUL
        ParsedHeader h;
        CHECK(!parseHeader(blob, h));
    }

    // ---- Non-printable byte in name -----------------------------------
    {
        std::vector<std::uint8_t> blob{
            0x00, 'B', 'a', 'd', 0x01, 'X', 0x00, 1, 2, 3, 4, 5, 6, 7
        };
        ParsedHeader h;
        CHECK(!parseHeader(blob, h));
    }

    // ---- No NUL within name cap ---------------------------------------
    {
        std::vector<std::uint8_t> blob;
        blob.push_back(0x00);
        for (int i = 0; i < 70; ++i) blob.push_back('A');  // > kNameMax
        blob.push_back(0x00);
        // pad to make sure size requirement isn't the rejecting factor
        for (int i = 0; i < 24; ++i) blob.push_back(0x00);
        ParsedHeader h;
        CHECK(!parseHeader(blob, h));
    }

    // ---- Truncated stat block — NUL present but < 7 bytes follow ------
    {
        std::vector<std::uint8_t> blob{
            0x00, 'X', 'Y', 0x00,
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06  // only 6 bytes after NUL
        };
        ParsedHeader h;
        CHECK(!parseHeader(blob, h));
    }

    // ---- Missing W/H/rot anchor — should reject ----------------------
    {
        // Build a blob whose stat-extra is present and 12 bytes of
        // filler follow, but no anchor pattern — parser must reject.
        std::vector<std::uint8_t> blob;
        blob.push_back(0x00);
        for (const char* p = "Junk"; *p; ++p) blob.push_back(static_cast<std::uint8_t>(*p));
        blob.push_back(0x00);
        blob.push_back(1); blob.push_back(2); blob.push_back(3);          // stat
        blob.push_back(0xAA); blob.push_back(0xBB);
        blob.push_back(0xCC); blob.push_back(0xDD);                       // stat extra
        for (std::size_t i = 0; i < 64; ++i) blob.push_back(0x42);        // filler, no anchor
        ParsedHeader h;
        CHECK(!parseHeader(blob, h));
    }

    EXIT_WITH_RESULT();
}
