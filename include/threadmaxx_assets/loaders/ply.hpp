#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "../data/mesh.hpp"
#include "../types.hpp"

namespace threadmaxx::assets {

// Loads a Stanford PLY file (binary little-endian or ASCII).
//
// Supported subset:
//   - element vertex: x/y/z (required), nx/ny/nz (optional), s/t (optional)
//   - element face: list uchar int vertex_indices (triangles or fans)
//   - format ascii 1.0 or format binary_little_endian 1.0
// Big-endian + non-standard property types return UnsupportedFormat.
AssetResult<MeshData> loadPly(std::string_view path);

AssetResult<MeshData> parsePly(std::span<const std::byte> bytes,
                               std::string_view sourcePath = {});

} // namespace threadmaxx::assets
