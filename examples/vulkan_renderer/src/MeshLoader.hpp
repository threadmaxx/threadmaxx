#pragma once

#include "VulkanContext.hpp"

#include <threadmaxx/Resource.hpp>
#include <threadmaxx_vk/Mesh.hpp>

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

namespace threadmaxx_vk {

/// v1 mesh loader: doesn't read from disk — exposes a single procedural
/// "unit cube" mesh that the smoke build uses. Real file I/O is deferred
/// to batch 10. Hot reload is a no-op for this loader (cubes don't
/// reload).
class MeshLoader : public threadmaxx::IResourceLoader {
public:
    explicit MeshLoader(VulkanContext& ctx) : ctx_(ctx) {}
    ~MeshLoader() override = default;

    void update(threadmaxx::Engine& /*engine*/) override {}
    void onShutdown(threadmaxx::Engine& engine) override;

    threadmaxx::LoaderStats stats() const noexcept override;

    /// Build the unit cube mesh in registry. Call once after Vulkan is
    /// up and the registry is reachable via the engine. Idempotent: a
    /// second call replaces the existing entry.
    threadmaxx::ResourceHandle<Mesh> createUnitCube(threadmaxx::Engine& engine);

    /// §3.11 batch 9b.2 — generic upload path. Accepts a flat vertex
    /// stream matching the opaque pipeline's binding-0 layout (6
    /// floats per corner: posXYZ + normalXYZ, 24-byte stride) and a
    /// 16-bit index array. Creates host-visible VkBuffers, copies the
    /// data, registers a new refcounted Mesh slot in the engine's
    /// resource registry, and returns the handle. The loader tracks
    /// the GPU memory so it can be freed at shutdown.
    ///
    /// @pre Vulkan context is up. @pre vertices.size() %
    /// `vertexStrideFloats` == 0. @pre indices.size() % 3 == 0 and
    /// indices contains only values within
    /// `[0, vertices.size() / vertexStrideFloats)`. Inputs that fail
    /// these preconditions return an invalid handle.
    ///
    /// `vertexStrideFloats` defaults to 6 for the legacy unskinned
    /// 24-byte vertex (pos[3]+normal[3]). The skinned 56-byte vertex
    /// (pos[3]+normal[3]+boneIDs[4]u32+boneWeights[4]f) needs 14.
    threadmaxx::ResourceHandle<Mesh> createMesh(
        threadmaxx::Engine&             engine,
        std::span<const float>          vertices,
        std::span<const std::uint16_t>  indices,
        std::uint32_t                   vertexStrideFloats = 6);

    /// Called by @ref VulkanRenderer::shutdown immediately after
    /// `vkDeviceWaitIdle` and before the Vulkan context is destroyed.
    /// Frees GPU memory while @ref ctx_ is still valid; the engine's
    /// later `onShutdown` is then guaranteed to be a no-op.
    void releaseGpuResources() noexcept;

private:
    VulkanContext& ctx_;
    threadmaxx::ResourceHandle<Mesh> unitCube_;
    /// GPU memory tracked here so the loader can free it at shutdown.
    /// The @ref Mesh stored in the registry only holds non-owning
    /// VkBuffer / VkDeviceMemory handles.
    std::vector<Mesh>                ownedMeshes_;
    std::atomic<std::uint64_t>       resident_ = 0;
};

} // namespace threadmaxx_vk
