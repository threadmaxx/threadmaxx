#pragma once

#include "VulkanContext.hpp"

#include <threadmaxx/Resource.hpp>
#include <threadmaxx_vk/Mesh.hpp>

#include <atomic>
#include <cstdint>
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
