#pragma once

#include <cstddef>

namespace threadmaxx::assets {

inline constexpr std::size_t kInitialMeshVertexCapacity   = 1024;
inline constexpr std::size_t kInitialMeshIndexCapacity    = 3 * kInitialMeshVertexCapacity;
inline constexpr std::size_t kInitialTextureBytesCapacity = 256 * 256 * 4;
inline constexpr std::size_t kInitialAudioBytesCapacity   = 48000 * 2 * 4; // 1 s float32 stereo
inline constexpr std::size_t kInitialFontGlyphCapacity    = 256;

inline constexpr std::size_t kRegistryInitialSlotCapacity = 256;
inline constexpr std::size_t kAsyncLoaderDefaultWorkers   = 2;

inline constexpr double kFilesystemWatchDefaultPollSeconds = 1.0;

} // namespace threadmaxx::assets
