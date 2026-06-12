#include "threadmaxx_assets/loaders/bmp.hpp"

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

AssetResult<TextureData> parseBmp(std::span<const std::byte> bytes,
                                  std::string_view sourcePath) {
    if (bytes.size() < 54) {
        return AssetResult<TextureData>::failure(
            ErrorCode::Truncated, "BMP shorter than header");
    }
    if (static_cast<char>(bytes[0]) != 'B' ||
        static_cast<char>(bytes[1]) != 'M') {
        return AssetResult<TextureData>::failure(
            ErrorCode::BadMagic, "missing BM signature");
    }

    const auto pixelOffset = readLE<std::uint32_t>(bytes, 10);
    const auto dibSize     = readLE<std::uint32_t>(bytes, 14);
    if (dibSize < 40) {
        return AssetResult<TextureData>::failure(
            ErrorCode::UnsupportedFormat, "non-BITMAPINFOHEADER BMP");
    }
    const auto width   = readLE<std::int32_t>(bytes, 18);
    const auto heightS = readLE<std::int32_t>(bytes, 22);
    const auto planes  = readLE<std::uint16_t>(bytes, 26);
    const auto bpp     = readLE<std::uint16_t>(bytes, 28);
    const auto compression = readLE<std::uint32_t>(bytes, 30);

    if (planes != 1 || (bpp != 24 && bpp != 32) || compression != 0) {
        return AssetResult<TextureData>::failure(
            ErrorCode::UnsupportedFormat,
            "BMP must be uncompressed 24/32 bpp single-plane");
    }
    if (width <= 0) {
        return AssetResult<TextureData>::failure(
            ErrorCode::ParseError, "BMP non-positive width");
    }

    const bool topDown = heightS < 0;
    const auto height = static_cast<std::uint32_t>(topDown ? -heightS : heightS);
    if (height == 0) {
        return AssetResult<TextureData>::failure(
            ErrorCode::ParseError, "BMP zero height");
    }

    const auto w = static_cast<std::uint32_t>(width);
    const std::uint32_t bytesPerPx = bpp / 8u;
    // BMP rows are padded to a 4-byte boundary.
    const std::uint32_t rowStride = ((w * bytesPerPx + 3u) / 4u) * 4u;

    if (pixelOffset + static_cast<std::uint64_t>(rowStride) * height > bytes.size()) {
        return AssetResult<TextureData>::failure(
            ErrorCode::Truncated, "BMP pixel data short");
    }

    TextureData out;
    out.width  = w;
    out.height = height;
    out.format = (bpp == 32) ? PixelFormat::RGBA8 : PixelFormat::RGB8;
    out.srgb   = true;
    out.sourcePath = std::string(sourcePath);

    const std::uint32_t dstStride = w * bytesPerPx;
    out.pixels.resize(static_cast<std::size_t>(dstStride) * height);

    const std::byte* src = bytes.data() + pixelOffset;
    for (std::uint32_t y = 0; y < height; ++y) {
        const std::uint32_t srcRow = topDown ? y : (height - 1 - y);
        const std::byte* srcLine = src + static_cast<std::size_t>(srcRow) * rowStride;
        std::byte* dstLine = out.pixels.data() + static_cast<std::size_t>(y) * dstStride;
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::byte* sp = srcLine + static_cast<std::size_t>(x) * bytesPerPx;
            std::byte* dp = dstLine + static_cast<std::size_t>(x) * bytesPerPx;
            // BMP is BGR(A); swap to RGB(A).
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

AssetResult<TextureData> loadBmp(std::string_view path) {
    auto bytes = detail::readFile(path);
    if (!bytes.ok()) {
        return AssetResult<TextureData>::failure(bytes.code,
                                                 std::move(bytes.message));
    }
    return parseBmp(bytes.value, path);
}

} // namespace threadmaxx::assets
