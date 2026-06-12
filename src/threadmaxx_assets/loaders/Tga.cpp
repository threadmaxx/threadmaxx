#include "threadmaxx_assets/loaders/tga.hpp"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "threadmaxx_assets/detail/io.hpp"

namespace threadmaxx::assets {

namespace {

template <class T>
T readLE(std::span<const std::byte> bytes, std::size_t off) noexcept {
    T v{};
    std::memcpy(&v, bytes.data() + off, sizeof(T));
    return v;
}

} // namespace

AssetResult<TextureData> parseTga(std::span<const std::byte> bytes,
                                  std::string_view sourcePath) {
    if (bytes.size() < 18) {
        return AssetResult<TextureData>::failure(
            ErrorCode::Truncated, "TGA shorter than header");
    }
    const auto idLen      = static_cast<std::uint32_t>(static_cast<std::uint8_t>(bytes[0]));
    const auto colorMap   = static_cast<std::uint8_t>(bytes[1]);
    const auto imageType  = static_cast<std::uint8_t>(bytes[2]);
    const auto width      = readLE<std::uint16_t>(bytes, 12);
    const auto height     = readLE<std::uint16_t>(bytes, 14);
    const auto bpp        = static_cast<std::uint8_t>(bytes[16]);
    const auto descriptor = static_cast<std::uint8_t>(bytes[17]);

    if (colorMap != 0) {
        return AssetResult<TextureData>::failure(
            ErrorCode::UnsupportedFormat, "TGA color map not supported");
    }
    if (imageType != 2 && imageType != 10) {
        return AssetResult<TextureData>::failure(
            ErrorCode::UnsupportedFormat,
            "TGA only types 2 and 10 supported");
    }
    if (bpp != 24 && bpp != 32) {
        return AssetResult<TextureData>::failure(
            ErrorCode::UnsupportedFormat, "TGA needs 24 or 32 bpp");
    }
    if (width == 0 || height == 0) {
        return AssetResult<TextureData>::failure(
            ErrorCode::ParseError, "TGA zero dimension");
    }

    const std::size_t headerSize = 18 + idLen;
    if (headerSize > bytes.size()) {
        return AssetResult<TextureData>::failure(
            ErrorCode::Truncated, "TGA id length exceeds buffer");
    }

    const std::uint32_t bytesPerPx = bpp / 8u;
    std::vector<std::byte> raw(static_cast<std::size_t>(width) * height * bytesPerPx);

    const std::byte* src = bytes.data() + headerSize;
    const std::byte* end = bytes.data() + bytes.size();

    if (imageType == 2) {
        const std::size_t need = raw.size();
        if (static_cast<std::size_t>(end - src) < need) {
            return AssetResult<TextureData>::failure(
                ErrorCode::Truncated, "TGA pixels truncated");
        }
        std::memcpy(raw.data(), src, need);
    } else {
        // RLE: each packet is 1 byte header + N pixels.
        std::size_t pixCount = static_cast<std::size_t>(width) * height;
        std::size_t pi = 0;
        while (pi < pixCount) {
            if (src >= end) {
                return AssetResult<TextureData>::failure(
                    ErrorCode::Truncated, "TGA RLE truncated header");
            }
            const auto h = static_cast<std::uint8_t>(*src++);
            const std::uint32_t runLen = (h & 0x7Fu) + 1u;
            const bool isRle = (h & 0x80u) != 0;
            if (isRle) {
                if (src + bytesPerPx > end) {
                    return AssetResult<TextureData>::failure(
                        ErrorCode::Truncated, "TGA RLE truncated pixel");
                }
                for (std::uint32_t k = 0; k < runLen && pi < pixCount; ++k) {
                    std::memcpy(raw.data() + pi * bytesPerPx, src, bytesPerPx);
                    ++pi;
                }
                src += bytesPerPx;
            } else {
                const std::size_t need = static_cast<std::size_t>(runLen) * bytesPerPx;
                if (src + need > end) {
                    return AssetResult<TextureData>::failure(
                        ErrorCode::Truncated, "TGA raw run truncated");
                }
                for (std::uint32_t k = 0; k < runLen && pi < pixCount; ++k) {
                    std::memcpy(raw.data() + pi * bytesPerPx,
                                src + static_cast<std::size_t>(k) * bytesPerPx,
                                bytesPerPx);
                    ++pi;
                }
                src += need;
            }
        }
    }

    TextureData out;
    out.width  = width;
    out.height = height;
    out.format = (bpp == 32) ? PixelFormat::RGBA8 : PixelFormat::RGB8;
    out.srgb   = true;
    out.sourcePath = std::string(sourcePath);
    out.pixels.resize(static_cast<std::size_t>(width) * height * bytesPerPx);

    // Descriptor bit 5 set → top-left origin; otherwise bottom-left.
    const bool topLeft = (descriptor & 0x20u) != 0;
    const std::uint32_t stride = width * bytesPerPx;

    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint32_t srcRow = topLeft ? y : (height - 1u - y);
        const std::byte* srcLine = raw.data() + static_cast<std::size_t>(srcRow) * stride;
        std::byte* dstLine = out.pixels.data() + static_cast<std::size_t>(y) * stride;
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::byte* sp = srcLine + static_cast<std::size_t>(x) * bytesPerPx;
            std::byte* dp = dstLine + static_cast<std::size_t>(x) * bytesPerPx;
            // TGA stores BGR(A) like BMP.
            dp[0] = sp[2];
            dp[1] = sp[1];
            dp[2] = sp[0];
            if (bytesPerPx == 4) {
                dp[3] = sp[3];
            }
        }
    }

    return AssetResult<TextureData>::success(std::move(out));
}

AssetResult<TextureData> loadTga(std::string_view path) {
    auto bytes = detail::readFile(path);
    if (!bytes.ok()) {
        return AssetResult<TextureData>::failure(bytes.code,
                                                 std::move(bytes.message));
    }
    return parseTga(bytes.value, path);
}

} // namespace threadmaxx::assets
