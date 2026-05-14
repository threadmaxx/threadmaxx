#pragma once

#include "../Components.hpp"  // Transform
#include "../Handles.hpp"

#include <array>
#include <cstdint>

namespace threadmaxx {

/// Phantom tag identifying a skeleton resource. Used as the type
/// parameter of `ResourceId<Skeleton>` from @ref MeshSkinnedRef.
struct Skeleton;

/// Phantom tag identifying a per-frame animation-pose buffer. Used as
/// the type parameter of `ResourceId<PoseBuffer>` from
/// @ref AnimationPoseRef. The renderer typically owns the actual GPU
/// pose buffer; the engine just carries the reference.
struct PoseBuffer;

/// Per-instance material parameter override. Four floats is enough for
/// the common case (tint + roughness/metallic scalar override + alpha);
/// renderers that need more shape can ignore some fields or extend with
/// a custom layout via @ref InstanceBufferLayout.
struct MaterialOverride {
    std::array<float, 4> params = {1.0f, 1.0f, 1.0f, 1.0f};
};

/// Reference to a skinned mesh + skeleton pairing. The actual mesh data
/// (vertex / index buffers) and skeleton data (bone hierarchy, inverse
/// bind matrices) are loaded by user @ref IResourceLoader implementations
/// and held in the engine's @ref ResourceRegistry.
struct MeshSkinnedRef {
    ResourceId<class Mesh>     mesh;
    ResourceId<Skeleton>       skeleton;
};

/// Reference to a per-frame animation pose. Renderers typically allocate
/// a ring of pose buffers (one per in-flight frame) and write per-bone
/// matrices into slot @ref ringSlot before issuing the draw. The engine
/// carries the reference; nothing per-bone lives in @ref EntityStorage.
///
/// @ref ringSlot is renderer-defined: it can be a GPU-buffer offset, an
/// index into a CPU-side pose array, or any other addressing scheme. The
/// engine never interprets it.
struct AnimationPoseRef {
    ResourceId<PoseBuffer> buffer;
    std::uint32_t          ringSlot = 0;
    std::uint32_t          boneCount = 0;
};

/// One draw call's worth of data, flattened to a POD the renderer can
/// consume directly. Filled in by user systems through @ref
/// RenderFrameBuilder::addDrawItem during the @ref
/// ISystem::buildRenderFrame hook.
///
/// The DrawItem is renderer-neutral: an OpenGL, Vulkan, D3D12, WebGPU,
/// or pure-software backend can all consume it without translation.
struct DrawItem {
    EntityHandle entity = kInvalidEntity;
    Transform    transform = {};

    /// Mesh resource id. Negative values are not legal here (DrawItems
    /// are only added for renderable entities); use the inverse — don't
    /// add the DrawItem — to indicate "no mesh."
    std::int32_t meshId = -1;
    std::int32_t materialId = -1;

    /// Skinned attachment. `skeletonId < 0` means non-skinned.
    std::int32_t skeletonId = -1;
    AnimationPoseRef pose = {};

    /// Per-instance material parameter override; renderer multiplies
    /// these in with the material's base values.
    MaterialOverride materialOverride = {};

    /// Bitmask of camera indices (into the RenderFrame's `cameras` span)
    /// that this draw item is visible to. The default value (all bits
    /// set) means "visible to every camera" — what the visibility
    /// culling system writes during its pass. A renderer iterating
    /// camera `c` filters items by `(cameraMask >> c) & 1`.
    std::uint32_t cameraMask = ~0u;

    /// Renderer-defined sort key. Renderers typically sort the bin's
    /// items by this before emitting draw calls (e.g. depth-front-to-
    /// back for opaque, back-to-front for transparent).
    std::uint64_t sortKey = 0;

    /// Free user-defined bits the renderer may interpret (visibility
    /// hint, lod index, custom shader variant, ...).
    std::uint32_t flags = 0;
};

} // namespace threadmaxx
