// tou2d_shp_import_test — pins the .SHP header parser.
//
// The Tier-2 SHP importer (`examples/tou2d/tou2d_import_shp.cpp`)
// decodes a stable prefix of a TOU ship file: a 0x00 padding byte, a
// NUL-terminated ASCII display name, and a 3-byte stat triplet. The
// rest of the file is opaque body. This test drives the parser
// directly against synthetic byte streams — no real .SHP file
// required, so it runs in any CI tree.
//
// Cases covered:
//   * Happy path: matches the byte layout observed in PERH.SHP, with
//     name "Butterfly" and the stat-triplet bytes (0x02 0x02 0x01)
//     observed in the actual stock file.
//   * Empty input.
//   * Wrong leading byte (anything non-zero).
//   * Name length zero (NUL immediately at offset 1).
//   * Non-printable byte inside the name.
//   * No NUL terminator within the 64-byte name cap.
//   * Truncated stat triplet (NUL hit but < 3 bytes left).
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

std::vector<std::uint8_t> makeBlob(const char* name,
                                   std::uint8_t s0,
                                   std::uint8_t s1,
                                   std::uint8_t s2,
                                   std::size_t  trailingBodyBytes = 0) {
    std::vector<std::uint8_t> b;
    b.push_back(0x00);
    for (const char* p = name; *p; ++p) {
        b.push_back(static_cast<std::uint8_t>(*p));
    }
    b.push_back(0x00);   // name terminator
    b.push_back(s0);
    b.push_back(s1);
    b.push_back(s2);
    for (std::size_t i = 0; i < trailingBodyBytes; ++i) {
        b.push_back(0xAA);
    }
    return b;
}

} // namespace

int main() {
    using tou2d::shp::ParsedHeader;
    using tou2d::shp::parseHeader;

    // ---- Happy path: real-PERH.SHP-shaped input -----------------------
    {
        // Matches the leading bytes of TOU/ships/PERH.SHP exactly,
        // including the (0x02, 0x02, 0x01) triplet observed in hex.
        const auto blob = makeBlob("Butterfly", 0x02, 0x02, 0x01,
                                   /*trailingBodyBytes=*/16);
        ParsedHeader h;
        CHECK(parseHeader(blob, h));
        CHECK_EQ(h.displayName, std::string{"Butterfly"});
        CHECK_EQ(h.statTriplet[0], std::uint8_t{0x02});
        CHECK_EQ(h.statTriplet[1], std::uint8_t{0x02});
        CHECK_EQ(h.statTriplet[2], std::uint8_t{0x01});
        // payloadStart = 1 (pad) + 9 (name) + 1 (NUL) + 3 (stats) = 14
        CHECK_EQ(h.payloadStart, std::size_t{14});
    }

    // ---- "Destroyer" — second stock-file shape ------------------------
    {
        const auto blob = makeBlob("Destroyer", 0x06, 0x02, 0x06, 8);
        ParsedHeader h;
        CHECK(parseHeader(blob, h));
        CHECK_EQ(h.displayName, std::string{"Destroyer"});
        CHECK_EQ(h.statTriplet[0], std::uint8_t{0x06});
        CHECK_EQ(h.statTriplet[2], std::uint8_t{0x06});
        CHECK_EQ(h.payloadStart, std::size_t{14});
    }

    // ---- Empty input --------------------------------------------------
    {
        const std::vector<std::uint8_t> empty;
        ParsedHeader h;
        CHECK(!parseHeader(empty, h));
    }

    // ---- Too-small input (< 5 bytes) ---------------------------------
    {
        const std::vector<std::uint8_t> tiny{0x00, 'A', 0x00};
        ParsedHeader h;
        CHECK(!parseHeader(tiny, h));
    }

    // ---- Wrong leading byte ------------------------------------------
    {
        auto blob = makeBlob("Anything", 1, 2, 3, 4);
        blob[0] = 0xFF;
        ParsedHeader h;
        CHECK(!parseHeader(blob, h));
    }

    // ---- Empty name (NUL at offset 1) ---------------------------------
    {
        std::vector<std::uint8_t> blob{0x00, 0x00, 0x01, 0x02, 0x03};
        ParsedHeader h;
        CHECK(!parseHeader(blob, h));
    }

    // ---- Non-printable byte in name -----------------------------------
    {
        // "Bad\x01name" — \x01 is below 0x20.
        std::vector<std::uint8_t> blob{
            0x00, 'B', 'a', 'd', 0x01, 'X', 0x00, 1, 2, 3
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
        blob.push_back(1);
        blob.push_back(2);
        blob.push_back(3);
        ParsedHeader h;
        CHECK(!parseHeader(blob, h));
    }

    // ---- Truncated stats — NUL present but < 3 stat bytes follow ------
    {
        std::vector<std::uint8_t> blob{
            0x00, 'X', 'Y', 0x00, 0x01, 0x02 /* missing third stat byte */
        };
        ParsedHeader h;
        CHECK(!parseHeader(blob, h));
    }

    EXIT_WITH_RESULT();
}
