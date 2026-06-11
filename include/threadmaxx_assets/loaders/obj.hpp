#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "../data/mesh.hpp"
#include "../types.hpp"

namespace threadmaxx::assets {

// Loads a Wavefront OBJ file from disk.
//
// Supported subset:
//   - v / vn / vt / f index forms a, a/b, a//c, a/b/c
//   - usemtl / g / o group splits (one submesh per usemtl)
//   - polygons up to 16 verts (fan-around-vertex-0)
// Missing vn → smoothed per-vertex normals from face normals.
// Missing vt → (0, 0).
//
// On success, MeshData::aabb is computed; submeshes are populated.
AssetResult<MeshData> loadObj(std::string_view path);

// In-memory variant. `sourcePath` is round-tripped into MeshData::sourcePath
// but no file is opened.
AssetResult<MeshData> parseObj(std::span<const std::byte> bytes,
                               std::string_view sourcePath = {});

} // namespace threadmaxx::assets
