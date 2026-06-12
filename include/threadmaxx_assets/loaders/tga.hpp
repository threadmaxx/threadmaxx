#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "../data/texture.hpp"
#include "../types.hpp"

namespace threadmaxx::assets {

// Loads a Truevision TGA file.
//
// Supported subset:
//   - image type 2 (uncompressed RGB), 24 or 32 bpp
//   - image type 10 (RLE RGB), 24 or 32 bpp
// Output is top-left-origin RGB8 / RGBA8. Bottom-up files are flipped.
AssetResult<TextureData> loadTga(std::string_view path);
AssetResult<TextureData> parseTga(std::span<const std::byte> bytes,
                                  std::string_view sourcePath = {});

} // namespace threadmaxx::assets
