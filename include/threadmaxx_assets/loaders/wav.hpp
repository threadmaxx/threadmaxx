#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "../data/audio.hpp"
#include "../types.hpp"

namespace threadmaxx::assets {

// Loads a RIFF WAVE file.
//
// Supported subset:
//   - wFormatTag = 1 (PCM 16-bit signed integer, little-endian)
//   - wFormatTag = 3 (IEEE float 32-bit, little-endian)
//   - Mono or stereo
// Unknown chunks between `fmt ` and `data` are tolerated and skipped.
AssetResult<AudioClipData> loadWav(std::string_view path);
AssetResult<AudioClipData> parseWav(std::span<const std::byte> bytes,
                                    std::string_view sourcePath = {});

} // namespace threadmaxx::assets
