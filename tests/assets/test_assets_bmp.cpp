#include "Check.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "threadmaxx_assets/loaders/bmp.hpp"

using namespace threadmaxx::assets;

namespace {

void putLE16(std::vector<std::byte>& dst, std::uint16_t v) {
    dst.push_back(static_cast<std::byte>(v & 0xFFu));
    dst.push_back(static_cast<std::byte>((v >> 8) & 0xFFu));
}
void putLE32(std::vector<std::byte>& dst, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        dst.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFFu));
    }
}

// Synthetic 4x2 24-bit BMP, BGR order, padded rows. Bottom-up (height
// positive). Row 0 (bottom): solid red. Row 1 (top): solid blue.
std::vector<std::byte> build4x2Bmp24() {
    std::vector<std::byte> bmp;
    const std::uint32_t pixelOffset = 54;
    const std::uint32_t rowStride   = 12; // 4 px * 3 bytes = 12 (already aligned)
    const std::uint32_t imageSize   = rowStride * 2;
    const std::uint32_t fileSize    = pixelOffset + imageSize;

    bmp.push_back(std::byte{'B'});
    bmp.push_back(std::byte{'M'});
    putLE32(bmp, fileSize);
    putLE16(bmp, 0); putLE16(bmp, 0);
    putLE32(bmp, pixelOffset);

    putLE32(bmp, 40);                // DIB size
    putLE32(bmp, 4u);                // width
    putLE32(bmp, 2u);                // height (positive = bottom-up)
    putLE16(bmp, 1u);                // planes
    putLE16(bmp, 24u);               // bpp
    putLE32(bmp, 0u);                // compression
    putLE32(bmp, imageSize);
    putLE32(bmp, 2835u);
    putLE32(bmp, 2835u);
    putLE32(bmp, 0u);
    putLE32(bmp, 0u);

    // Row 0 (bottom = output row 1 after flip): pure red.
    for (int x = 0; x < 4; ++x) {
        bmp.push_back(std::byte{0x00}); // B
        bmp.push_back(std::byte{0x00}); // G
        bmp.push_back(std::byte{0xFF}); // R
    }
    // Row 1 (top = output row 0 after flip): pure blue.
    for (int x = 0; x < 4; ++x) {
        bmp.push_back(std::byte{0xFF}); // B
        bmp.push_back(std::byte{0x00}); // G
        bmp.push_back(std::byte{0x00}); // R
    }
    return bmp;
}

} // namespace

int main() {
    const auto bytes = build4x2Bmp24();
    auto r = parseBmp(std::span<const std::byte>(bytes), "fake.bmp");
    CHECK(r.ok());
    if (!r.ok()) {
        EXIT_WITH_RESULT();
    }
    const auto& t = r.value;
    CHECK_EQ(t.width,  4u);
    CHECK_EQ(t.height, 2u);
    CHECK(t.format == PixelFormat::RGB8);
    CHECK_EQ(t.pixels.size(), std::size_t{24});

    // Output is top-left-origin RGB. Row 0 was the "top" row = blue.
    CHECK_EQ(static_cast<unsigned>(t.pixels[0]), 0u);   // R
    CHECK_EQ(static_cast<unsigned>(t.pixels[1]), 0u);   // G
    CHECK_EQ(static_cast<unsigned>(t.pixels[2]), 0xFFu); // B

    // Row 1 = the bottom row in the file = red.
    CHECK_EQ(static_cast<unsigned>(t.pixels[12]), 0xFFu);
    CHECK_EQ(static_cast<unsigned>(t.pixels[13]), 0u);
    CHECK_EQ(static_cast<unsigned>(t.pixels[14]), 0u);

    // Bad magic.
    std::vector<std::byte> bad(54, std::byte{0});
    auto br = parseBmp(bad, "");
    CHECK(!br.ok());
    CHECK(br.code == ErrorCode::BadMagic);

    EXIT_WITH_RESULT();
}
