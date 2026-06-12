#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "../data/texture.hpp"
#include "../types.hpp"

namespace threadmaxx::assets {

// Loads a PNG file.
//
// Supported subset:
//   - color type 2 (RGB) and 6 (RGBA)
//   - bit depth 8
//   - not interlaced
// Other variants return UnsupportedFormat. PNG carries top-left origin
// so no flip is applied.
AssetResult<TextureData> loadPng(std::string_view path);
AssetResult<TextureData> parsePng(std::span<const std::byte> bytes,
                                  std::string_view sourcePath = {});

} // namespace threadmaxx::assets
