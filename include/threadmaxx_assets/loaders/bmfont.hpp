#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "../data/font.hpp"
#include "../types.hpp"

namespace threadmaxx::assets {

// Loads a BMFont descriptor (.fnt) plus all referenced page PNGs.
//
// Both BMFont text format (info / common / page / chars / kernings
// lines) and BMFont v3 binary format (block-tagged) are supported.
// Page filenames in the .fnt are resolved relative to the .fnt's own
// directory and parsed through loadPng.
AssetResult<FontAtlas> loadBmfont(std::string_view path);

// In-memory variant. Skips page loading when `assetDir` is empty.
AssetResult<FontAtlas> parseBmfont(std::span<const std::byte> bytes,
                                   std::string_view sourcePath = {},
                                   std::string_view assetDir   = {});

} // namespace threadmaxx::assets
