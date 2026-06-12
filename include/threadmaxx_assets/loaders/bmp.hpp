#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "../data/texture.hpp"
#include "../types.hpp"

namespace threadmaxx::assets {

// BITMAPINFOHEADER-only BMP loader. Supports uncompressed 24-bit BGR
// and 32-bit BGRA files. Output is top-left-origin RGBA8 (or RGB8 if
// the file was 24-bit).
AssetResult<TextureData> loadBmp(std::string_view path);
AssetResult<TextureData> parseBmp(std::span<const std::byte> bytes,
                                  std::string_view sourcePath = {});

} // namespace threadmaxx::assets
