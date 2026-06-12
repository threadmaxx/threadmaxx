#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "data/audio.hpp"
#include "data/font.hpp"
#include "data/mesh.hpp"
#include "data/texture.hpp"
#include "registry.hpp"
#include "types.hpp"

namespace threadmaxx::assets {

// Cooked-bundle aggregate. One on-disk file per Bundle; loads in one
// shot at startup to bypass the per-format parsers in shipping builds.
struct Bundle {
    std::vector<std::pair<std::string, MeshData>>      meshes;
    std::vector<std::pair<std::string, TextureData>>   textures;
    std::vector<std::pair<std::string, AudioClipData>> audio;
    std::vector<std::pair<std::string, FontAtlas>>     fonts;
};

inline constexpr std::uint32_t kBundleMagic   = 0x53414D54u; // 'TMAS'
inline constexpr std::uint32_t kBundleVersion = 1u;

AssetResult<std::vector<std::byte>> writeBundle(const Bundle& b);
AssetResult<Bundle> readBundle(std::span<const std::byte> bytes);
AssetResult<Bundle> readBundleFromFile(std::string_view path);

// RAII handle bag returned by mountBundleInto. While the holder lives,
// the registry slots stay alive. Drop it to release the bundle's
// references; consumers that took their own handles via findX are
// unaffected.
struct BundleMount {
    std::vector<AssetHandle<MeshData>>      meshes;
    std::vector<AssetHandle<TextureData>>   textures;
    std::vector<AssetHandle<AudioClipData>> audio;
    std::vector<AssetHandle<FontAtlas>>     fonts;
};

// Mount every record in `b` into `reg` under its bundle name. Identical
// names dedup against any in-flight slot (see AssetRegistry::addX).
BundleMount mountBundleInto(AssetRegistry& reg, const Bundle& b);

} // namespace threadmaxx::assets
