#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace threadmaxx::assets {

enum class PixelFormat : std::uint8_t {
    Unknown = 0,
    R8,
    RG8,
    RGB8,
    RGBA8
};

[[nodiscard]] constexpr std::uint32_t bytesPerPixel(PixelFormat f) noexcept {
    switch (f) {
        case PixelFormat::R8:    return 1;
        case PixelFormat::RG8:   return 2;
        case PixelFormat::RGB8:  return 3;
        case PixelFormat::RGBA8: return 4;
        case PixelFormat::Unknown: break;
    }
    return 0;
}

struct TextureData {
    std::uint32_t           width{};
    std::uint32_t           height{};
    PixelFormat             format{PixelFormat::Unknown};
    bool                    srgb{true};
    std::vector<std::byte>  pixels;
    std::string             sourcePath;
};

} // namespace threadmaxx::assets
