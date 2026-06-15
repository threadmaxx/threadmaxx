#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "mesh.hpp"
#include "../types.hpp"

/// glTF 2.0 / .glb (binary glTF) parsed scene PODs.
///
/// The glTF spec carries far more state than `MeshData` alone can
/// express — skins, animations, multiple meshes per scene, parent
/// node transforms. This header defines the additional POD lanes
/// that the loader populates alongside `MeshData`. Game code stays
/// free to keep using `MeshData` directly when it only needs the
/// static mesh; the richer fields are opt-in.
///
/// All math types are flat float arrays (no math-lib dependency).
/// Matrix layout is column-major, glTF 2.0 convention.
namespace threadmaxx::assets {

/// Per-vertex skinning influences. Parallel to `MeshData::vertices`
/// (1:1 indexing). Empty when the mesh has no JOINTS_0 / WEIGHTS_0
/// accessors.
struct SkinnedVertex {
    std::uint16_t joints[4]{};
    float         weights[4]{};
};

/// Single joint in a skin. `parent == -1` marks a root joint (the
/// hierarchy may have multiple roots).
struct GltfJoint {
    std::string  name;
    int          parent{-1};
    float        translation[3]{0, 0, 0};
    float        rotation[4]{0, 0, 0, 1};       // x, y, z, w
    float        scale[3]{1, 1, 1};
    float        inverseBind[16]{                // column-major identity
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1,
    };
};

struct SkinData {
    std::string             name;
    std::vector<GltfJoint>  joints;
};

enum class AnimChannelPath : std::uint8_t {
    Translation = 0,
    Rotation    = 1,
    Scale       = 2,
    Weights     = 3,        // morph-target weights — accepted, not used in v1
};

enum class AnimInterpolation : std::uint8_t {
    Linear      = 0,        // LERP / SLERP for quats
    Step        = 1,        // hold previous keyframe
    CubicSpline = 2,        // tangent-in / value / tangent-out triplets
};

/// One animation channel — a single TRS lane on a single joint. The
/// `inputs` / `outputs` arrays are flat:
///   - inputs:  size = keyframeCount, sorted ascending, seconds
///   - outputs: size = keyframeCount * componentCount
///       (componentCount = 3 for translation/scale, 4 for rotation,
///        and 3x if interpolation == CubicSpline → in/value/out triplet)
struct AnimChannel {
    std::uint32_t      jointIndex{};            // index into SkinData::joints
    AnimChannelPath    path{AnimChannelPath::Translation};
    AnimInterpolation  interpolation{AnimInterpolation::Linear};
    std::vector<float> inputs;
    std::vector<float> outputs;
};

struct AnimationData {
    std::string                 name;
    float                       duration{};     // max(input) across all channels
    std::vector<AnimChannel>    channels;
};

/// One mesh in the glTF scene. `mesh` carries the merged vertex/index
/// data (one submesh per primitive, same as the OBJ loader). `skinned`
/// is parallel to `mesh.vertices` when the source primitives had skin
/// attributes; empty otherwise. `skinIndex` indexes into
/// `GltfSceneData::skins` (or `kInvalidAssetId` for unskinned meshes).
struct GltfMesh {
    MeshData                    mesh;
    std::vector<SkinnedVertex>  skinned;
    AssetId                     skinIndex{kInvalidAssetId};
};

/// Top-level parsed-scene POD. Multiple meshes / skins / animations
/// are accumulated in the order they appear in the file — game code
/// uses `name` to pick what it wants.
struct GltfSceneData {
    std::vector<GltfMesh>       meshes;
    std::vector<SkinData>       skins;
    std::vector<AnimationData>  animations;
    std::string                 sourcePath;
};

} // namespace threadmaxx::assets
