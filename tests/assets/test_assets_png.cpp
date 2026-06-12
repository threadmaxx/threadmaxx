#include "Check.hpp"

#include <cstdint>
#include <span>
#include <vector>

#include "threadmaxx_assets/loaders/png.hpp"

using namespace threadmaxx::assets;

namespace {

void appendBE32(std::vector<std::byte>& dst, std::uint32_t v) {
    dst.push_back(static_cast<std::byte>((v >> 24) & 0xFFu));
    dst.push_back(static_cast<std::byte>((v >> 16) & 0xFFu));
    dst.push_back(static_cast<std::byte>((v >>  8) & 0xFFu));
    dst.push_back(static_cast<std::byte>((v >>  0) & 0xFFu));
}

void appendChunk(std::vector<std::byte>& dst, const char* type,
                 std::span<const std::byte> data) {
    appendBE32(dst, static_cast<std::uint32_t>(data.size()));
    dst.push_back(static_cast<std::byte>(type[0]));
    dst.push_back(static_cast<std::byte>(type[1]));
    dst.push_back(static_cast<std::byte>(type[2]));
    dst.push_back(static_cast<std::byte>(type[3]));
    dst.insert(dst.end(), data.begin(), data.end());
    // CRC32 placeholder; the loader does not verify chunk CRC.
    appendBE32(dst, 0u);
}

// Builds a 2x2 RGB PNG with 4 synthetic pixels: red / green / blue /
// yellow. Uses a single zlib stream containing one stored DEFLATE block.
std::vector<std::byte> build2x2Png() {
    std::vector<std::byte> png;

    // PNG signature.
    constexpr std::uint8_t sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    for (auto b : sig) png.push_back(static_cast<std::byte>(b));

    // IHDR.
    std::vector<std::byte> ihdr;
    appendBE32(ihdr, 2u);  // width
    appendBE32(ihdr, 2u);  // height
    ihdr.push_back(std::byte{8});  // bit depth
    ihdr.push_back(std::byte{2});  // color type = 2 (RGB)
    ihdr.push_back(std::byte{0});
    ihdr.push_back(std::byte{0});
    ihdr.push_back(std::byte{0});
    appendChunk(png, "IHDR", ihdr);

    // Raw filtered scanlines for the 2x2 image (top-left origin):
    // (255,0,0)  (0,255,0)
    // (0,0,255)  (255,255,0)
    const std::uint8_t scanlines[] = {
        0x00, 0xFF, 0x00, 0x00, 0x00, 0xFF, 0x00, // row 0, filter=None
        0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x00, // row 1, filter=None
    };
    const std::uint32_t LEN = sizeof(scanlines);

    std::vector<std::byte> idat;
    // zlib header: CMF=0x78, FLG=0x01 (passes the % 31 check).
    idat.push_back(std::byte{0x78});
    idat.push_back(std::byte{0x01});
    // DEFLATE: one stored final block. BFINAL=1, BTYPE=00 → byte 0x01.
    idat.push_back(std::byte{0x01});
    // LEN / NLEN little-endian.
    idat.push_back(std::byte{static_cast<std::uint8_t>(LEN & 0xFFu)});
    idat.push_back(std::byte{static_cast<std::uint8_t>((LEN >> 8) & 0xFFu)});
    const std::uint16_t NLEN = static_cast<std::uint16_t>(~LEN);
    idat.push_back(std::byte{static_cast<std::uint8_t>(NLEN & 0xFFu)});
    idat.push_back(std::byte{static_cast<std::uint8_t>((NLEN >> 8) & 0xFFu)});
    for (auto b : scanlines) idat.push_back(static_cast<std::byte>(b));
    // ADLER32 placeholder (loader does not verify).
    for (int i = 0; i < 4; ++i) idat.push_back(std::byte{0});

    appendChunk(png, "IDAT", idat);
    appendChunk(png, "IEND", {});
    return png;
}

} // namespace

int main() {
    const auto bytes = build2x2Png();
    auto r = parsePng(std::span<const std::byte>(bytes), "fake.png");
    CHECK(r.ok());
    if (!r.ok()) {
        EXIT_WITH_RESULT();
    }
    const auto& t = r.value;
    CHECK_EQ(t.width,  2u);
    CHECK_EQ(t.height, 2u);
    CHECK(t.format == PixelFormat::RGB8);
    CHECK_EQ(t.pixels.size(), std::size_t{12});

    // (0,0) = red
    CHECK_EQ(static_cast<unsigned>(t.pixels[0]), 0xFFu);
    CHECK_EQ(static_cast<unsigned>(t.pixels[1]), 0u);
    CHECK_EQ(static_cast<unsigned>(t.pixels[2]), 0u);
    // (1,0) = green
    CHECK_EQ(static_cast<unsigned>(t.pixels[3]), 0u);
    CHECK_EQ(static_cast<unsigned>(t.pixels[4]), 0xFFu);
    CHECK_EQ(static_cast<unsigned>(t.pixels[5]), 0u);
    // (0,1) = blue
    CHECK_EQ(static_cast<unsigned>(t.pixels[6]), 0u);
    CHECK_EQ(static_cast<unsigned>(t.pixels[7]), 0u);
    CHECK_EQ(static_cast<unsigned>(t.pixels[8]), 0xFFu);
    // (1,1) = yellow
    CHECK_EQ(static_cast<unsigned>(t.pixels[9]),  0xFFu);
    CHECK_EQ(static_cast<unsigned>(t.pixels[10]), 0xFFu);
    CHECK_EQ(static_cast<unsigned>(t.pixels[11]), 0u);

    // Bad signature.
    std::vector<std::byte> bad(8, std::byte{0});
    auto br = parsePng(bad, "");
    CHECK(!br.ok());
    CHECK(br.code == ErrorCode::BadMagic);

    EXIT_WITH_RESULT();
}
