// tou2d_pal_col_test — pins the TOU VGA palette loader.
//
// Drives the header-only loader against synthetic 768-byte palette
// blobs (no real `Pal.col` file needed in CI). Cases covered:
//   * Wrong size (rejected).
//   * Known entries — round-trips index 0 (all zero) and index 255
//     (all 0x3F → expanded to 0xFF).
//   * Bit-replication expansion produces the documented values
//     (0x00→0x00, 0x3F→0xFF, mid value 0x20→0x82 from 0b100000<<2 |
//     0b100000>>4).
//
// Independent of the engine; links threadmaxx::threadmaxx only for
// CMake glue uniformity.

#include "Check.hpp"

#include "../examples/tou2d/PalCol.hpp"

#include <cstdint>
#include <vector>

int main() {
    using tou2d::shp::parsePalette;
    using tou2d::shp::Palette;
    using tou2d::shp::expand6to8;

    // ---- expand6to8 bit-replication contract --------------------------
    CHECK_EQ(expand6to8(0x00), std::uint8_t{0x00});
    CHECK_EQ(expand6to8(0x3F), std::uint8_t{0xFF});
    // 0x20 = 0b100000; (0x20<<2)|(0x20>>4) = 0x80 | 0x02 = 0x82.
    CHECK_EQ(expand6to8(0x20), std::uint8_t{0x82});

    // ---- Wrong-size rejection ----------------------------------------
    {
        std::vector<std::uint8_t> tooSmall(767, 0);
        Palette pal;
        CHECK(!parsePalette(tooSmall, pal));
    }
    {
        std::vector<std::uint8_t> tooBig(769, 0);
        Palette pal;
        CHECK(!parsePalette(tooBig, pal));
    }

    // ---- Happy path — round-trip a synthetic palette ------------------
    {
        std::vector<std::uint8_t> raw(768, 0);
        // Entry 0: black, leave as zero.
        // Entry 1: max red.
        raw[3] = 0x3F; raw[4] = 0x00; raw[5] = 0x00;
        // Entry 2: mid green.
        raw[6] = 0x00; raw[7] = 0x20; raw[8] = 0x00;
        // Entry 255: max white.
        raw[765] = 0x3F; raw[766] = 0x3F; raw[767] = 0x3F;

        Palette pal;
        CHECK(parsePalette(raw, pal));
        CHECK_EQ(pal.entries[0].r, std::uint8_t{0x00});
        CHECK_EQ(pal.entries[0].g, std::uint8_t{0x00});
        CHECK_EQ(pal.entries[0].b, std::uint8_t{0x00});
        CHECK_EQ(pal.entries[1].r, std::uint8_t{0xFF});
        CHECK_EQ(pal.entries[1].g, std::uint8_t{0x00});
        CHECK_EQ(pal.entries[2].g, std::uint8_t{0x82});
        CHECK_EQ(pal.entries[255].r, std::uint8_t{0xFF});
        CHECK_EQ(pal.entries[255].g, std::uint8_t{0xFF});
        CHECK_EQ(pal.entries[255].b, std::uint8_t{0xFF});
    }

    EXIT_WITH_RESULT();
}
