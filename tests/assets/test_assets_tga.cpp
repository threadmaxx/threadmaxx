#include "Check.hpp"

#include <cstdint>
#include <span>
#include <vector>

#include "threadmaxx_assets/loaders/tga.hpp"

using namespace threadmaxx::assets;

namespace {

// Type-2 uncompressed 4x2 BGR top-left.
std::vector<std::byte> buildTga2Uncompressed() {
    std::vector<std::byte> tga(18, std::byte{0});
    tga[2] = std::byte{2};                          // image type
    tga[12] = std::byte{4};   tga[13] = std::byte{0};   // width LE
    tga[14] = std::byte{2};   tga[15] = std::byte{0};   // height LE
    tga[16] = std::byte{24};                          // bpp
    tga[17] = std::byte{0x20};                        // top-left origin

    // Row 0: green; Row 1: red.
    for (int x = 0; x < 4; ++x) {
        tga.push_back(std::byte{0x00}); // B
        tga.push_back(std::byte{0xFF}); // G
        tga.push_back(std::byte{0x00}); // R
    }
    for (int x = 0; x < 4; ++x) {
        tga.push_back(std::byte{0x00}); // B
        tga.push_back(std::byte{0x00}); // G
        tga.push_back(std::byte{0xFF}); // R
    }
    return tga;
}

// Type-10 RLE 4x1: one packet RLE-run of 4 blue pixels.
std::vector<std::byte> buildTgaRleSingleRow() {
    std::vector<std::byte> tga(18, std::byte{0});
    tga[2] = std::byte{10};                         // RLE RGB
    tga[12] = std::byte{4};   tga[13] = std::byte{0};
    tga[14] = std::byte{1};   tga[15] = std::byte{0};
    tga[16] = std::byte{24};
    tga[17] = std::byte{0x20};

    tga.push_back(std::byte{0x83});                  // RLE, run length 4
    tga.push_back(std::byte{0xFF}); // B
    tga.push_back(std::byte{0x00}); // G
    tga.push_back(std::byte{0x00}); // R
    return tga;
}

} // namespace

int main() {
    {
        const auto bytes = buildTga2Uncompressed();
        auto r = parseTga(std::span<const std::byte>(bytes), "fake.tga");
        CHECK(r.ok());
        if (!r.ok()) {
            EXIT_WITH_RESULT();
        }
        const auto& t = r.value;
        CHECK_EQ(t.width,  4u);
        CHECK_EQ(t.height, 2u);
        CHECK(t.format == PixelFormat::RGB8);
        // Row 0 = green.
        CHECK_EQ(static_cast<unsigned>(t.pixels[0]), 0u);
        CHECK_EQ(static_cast<unsigned>(t.pixels[1]), 0xFFu);
        CHECK_EQ(static_cast<unsigned>(t.pixels[2]), 0u);
        // Row 1 = red.
        CHECK_EQ(static_cast<unsigned>(t.pixels[12]), 0xFFu);
        CHECK_EQ(static_cast<unsigned>(t.pixels[13]), 0u);
        CHECK_EQ(static_cast<unsigned>(t.pixels[14]), 0u);
    }

    {
        const auto bytes = buildTgaRleSingleRow();
        auto r = parseTga(std::span<const std::byte>(bytes), "rle.tga");
        CHECK(r.ok());
        if (!r.ok()) {
            EXIT_WITH_RESULT();
        }
        const auto& t = r.value;
        CHECK_EQ(t.width,  4u);
        CHECK_EQ(t.height, 1u);
        for (std::uint32_t x = 0; x < 4; ++x) {
            CHECK_EQ(static_cast<unsigned>(t.pixels[x * 3 + 0]), 0u);
            CHECK_EQ(static_cast<unsigned>(t.pixels[x * 3 + 1]), 0u);
            CHECK_EQ(static_cast<unsigned>(t.pixels[x * 3 + 2]), 0xFFu);
        }
    }

    EXIT_WITH_RESULT();
}
