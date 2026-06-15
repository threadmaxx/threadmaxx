#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "../data/gltf.hpp"
#include "../data/mesh.hpp"
#include "../types.hpp"

namespace threadmaxx::assets {

// Loads a binary glTF (.glb) file from disk. The .glb container is a
// 12-byte header (`glTF` magic, version 2, length) followed by two
// chunks: JSON descriptor (0x4E4F534A) and BIN payload (0x004E4942).
//
// Supported subset:
//   - meshes/primitives with POSITION, NORMAL (optional), TEXCOORD_0
//     (optional), JOINTS_0 (optional), WEIGHTS_0 (optional), indices
//     (optional — flat triangle lists when absent)
//   - accessor componentTypes: 5120 (BYTE), 5121 (UNSIGNED_BYTE),
//     5122 (SHORT), 5123 (UNSIGNED_SHORT), 5125 (UNSIGNED_INT),
//     5126 (FLOAT)
//   - skins with inverseBindMatrices + joints list
//   - animations with samplers (LINEAR / STEP / CUBICSPLINE) and
//     channels targeting translation / rotation / scale
//   - node hierarchy collapsed during mesh extraction (each mesh
//     primitive's vertices are pre-multiplied by the owning node's
//     world transform — saves the consumer a tree walk)
//
// Not supported in v1.0:
//   - external `.gltf` + `.bin` pairs (only `.glb` for now)
//   - external image references (textures returned as ids only;
//     image decoding stays a separate batch)
//   - sparse accessors, morph targets in skinned playback
//   - extensions other than KHR_mesh_quantization (silently ignored)
//
// Static-only mesh consumers can use `loadGltfMesh()` to get a single
// merged `MeshData` for the entire scene (concatenated meshes; node
// transforms baked in). The full scene view is `loadGltfScene()`.
AssetResult<MeshData>       loadGltfMesh(std::string_view path);
AssetResult<GltfSceneData>  loadGltfScene(std::string_view path);

// In-memory variants. Caller owns the bytes; the parser returns
// failure with `ErrorCode::Truncated` / `ParseError` / `BadMagic`
// rather than aborting on malformed input.
AssetResult<MeshData>       parseGltfMesh(std::span<const std::byte> bytes,
                                          std::string_view sourcePath = {});
AssetResult<GltfSceneData>  parseGltfScene(std::span<const std::byte> bytes,
                                           std::string_view sourcePath = {});

} // namespace threadmaxx::assets
