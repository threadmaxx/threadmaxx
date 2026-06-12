#include "threadmaxx_assets/loaders/png.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "threadmaxx_assets/detail/inflate.hpp"
#include "threadmaxx_assets/detail/io.hpp"

namespace threadmaxx::assets {

namespace {

std::uint32_t readBE32(const std::byte* p) noexcept {
    return (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[0])) << 24) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[2])) <<  8) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[3])));
}

std::uint8_t paeth(std::uint8_t a, std::uint8_t b, std::uint8_t c) noexcept {
    const int pa = std::abs(static_cast<int>(b) - static_cast<int>(c));
    const int pb = std::abs(static_cast<int>(a) - static_cast<int>(c));
    const int pc = std::abs(static_cast<int>(a) + static_cast<int>(b) -
                            2 * static_cast<int>(c));
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

} // namespace

AssetResult<TextureData> parsePng(std::span<const std::byte> bytes,
                                  std::string_view sourcePath) {
    constexpr std::uint8_t kSig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (bytes.size() < 8) {
        return AssetResult<TextureData>::failure(
            ErrorCode::Truncated, "PNG shorter than signature");
    }
    for (std::size_t i = 0; i < 8; ++i) {
        if (static_cast<std::uint8_t>(bytes[i]) != kSig[i]) {
            return AssetResult<TextureData>::failure(
                ErrorCode::BadMagic, "PNG signature mismatch");
        }
    }

    std::uint32_t width = 0, height = 0;
    std::uint8_t  bitDepth = 0, colorType = 0, interlace = 0;
    std::vector<std::byte> idat;
    bool sawIHDR = false, sawIEND = false;

    std::size_t pos = 8;
    while (pos + 8 <= bytes.size()) {
        const std::uint32_t len = readBE32(bytes.data() + pos);
        const std::byte* type = bytes.data() + pos + 4;
        const std::byte* data = bytes.data() + pos + 8;
        const std::size_t advance = static_cast<std::size_t>(len) + 12;
        if (pos + advance > bytes.size()) {
            return AssetResult<TextureData>::failure(
                ErrorCode::Truncated, "PNG chunk truncated");
        }

        const char t0 = static_cast<char>(type[0]);
        const char t1 = static_cast<char>(type[1]);
        const char t2 = static_cast<char>(type[2]);
        const char t3 = static_cast<char>(type[3]);

        if (t0 == 'I' && t1 == 'H' && t2 == 'D' && t3 == 'R') {
            if (len < 13) {
                return AssetResult<TextureData>::failure(
                    ErrorCode::ParseError, "IHDR too short");
            }
            width    = readBE32(data);
            height   = readBE32(data + 4);
            bitDepth = static_cast<std::uint8_t>(data[8]);
            colorType = static_cast<std::uint8_t>(data[9]);
            // method (10), filter (11), interlace (12).
            interlace = static_cast<std::uint8_t>(data[12]);
            sawIHDR = true;
        } else if (t0 == 'I' && t1 == 'D' && t2 == 'A' && t3 == 'T') {
            idat.insert(idat.end(), data, data + len);
        } else if (t0 == 'I' && t1 == 'E' && t2 == 'N' && t3 == 'D') {
            sawIEND = true;
            pos += advance;
            break;
        }

        pos += advance;
    }

    if (!sawIHDR || !sawIEND) {
        return AssetResult<TextureData>::failure(
            ErrorCode::ParseError, "PNG missing IHDR or IEND");
    }
    if (bitDepth != 8) {
        return AssetResult<TextureData>::failure(
            ErrorCode::UnsupportedFormat, "PNG only 8-bit depth supported");
    }
    if (colorType != 2 && colorType != 6) {
        return AssetResult<TextureData>::failure(
            ErrorCode::UnsupportedFormat, "PNG only color types 2 and 6 supported");
    }
    if (interlace != 0) {
        return AssetResult<TextureData>::failure(
            ErrorCode::UnsupportedFormat, "PNG interlaced not supported");
    }
    if (width == 0 || height == 0) {
        return AssetResult<TextureData>::failure(
            ErrorCode::ParseError, "PNG zero dimension");
    }

    std::vector<std::byte> raw;
    raw.reserve(static_cast<std::size_t>(width) * height * 5);
    const auto err = detail::inflate(idat, raw, true);
    if (err != ErrorCode::Ok) {
        return AssetResult<TextureData>::failure(err, "PNG inflate failed");
    }

    const std::uint32_t channels = (colorType == 2) ? 3u : 4u;
    const std::uint32_t stride = width * channels;
    const std::size_t expected = static_cast<std::size_t>(height) * (stride + 1u);
    if (raw.size() < expected) {
        return AssetResult<TextureData>::failure(
            ErrorCode::Truncated, "PNG inflated bytes short");
    }

    TextureData out;
    out.width  = width;
    out.height = height;
    out.format = (channels == 4) ? PixelFormat::RGBA8 : PixelFormat::RGB8;
    out.srgb   = true;
    out.sourcePath = std::string(sourcePath);
    out.pixels.resize(static_cast<std::size_t>(height) * stride);

    std::vector<std::byte> prev(stride, std::byte{0});
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::byte* line = raw.data() + static_cast<std::size_t>(y) * (stride + 1u);
        const auto filter = static_cast<std::uint8_t>(line[0]);
        const std::byte* in = line + 1;
        std::byte* dst = out.pixels.data() + static_cast<std::size_t>(y) * stride;
        for (std::uint32_t x = 0; x < stride; ++x) {
            const std::uint8_t a = x >= channels
                ? static_cast<std::uint8_t>(dst[x - channels]) : 0u;
            const std::uint8_t b = static_cast<std::uint8_t>(prev[x]);
            const std::uint8_t c = x >= channels
                ? static_cast<std::uint8_t>(prev[x - channels]) : 0u;
            const std::uint8_t raw_b = static_cast<std::uint8_t>(in[x]);
            std::uint8_t v = raw_b;
            switch (filter) {
                case 0: break;
                case 1: v = static_cast<std::uint8_t>(raw_b + a); break;
                case 2: v = static_cast<std::uint8_t>(raw_b + b); break;
                case 3: v = static_cast<std::uint8_t>(raw_b + ((a + b) >> 1)); break;
                case 4: v = static_cast<std::uint8_t>(raw_b + paeth(a, b, c)); break;
                default:
                    return AssetResult<TextureData>::failure(
                        ErrorCode::ParseError, "PNG unknown filter byte");
            }
            dst[x] = static_cast<std::byte>(v);
        }
        std::memcpy(prev.data(), dst, stride);
    }

    return AssetResult<TextureData>::success(std::move(out));
}

AssetResult<TextureData> loadPng(std::string_view path) {
    auto bytes = detail::readFile(path);
    if (!bytes.ok()) {
        return AssetResult<TextureData>::failure(bytes.code,
                                                 std::move(bytes.message));
    }
    return parsePng(bytes.value, path);
}

} // namespace threadmaxx::assets
