#pragma once

#include "VulkanContext.hpp"

#include <threadmaxx/Resource.hpp>
#include <threadmaxx_vk/Mesh.hpp>  // Texture POD lives here

#include <atomic>
#include <cstdint>
#include <vector>

namespace threadmaxx_vk {

/// v1 texture loader: synthesizes a 1x1 white texture for use as the
/// fallback material. Real file I/O is deferred to batch 10.
class TextureLoader : public threadmaxx::IResourceLoader {
public:
    explicit TextureLoader(VulkanContext& ctx) : ctx_(ctx) {}
    ~TextureLoader() override = default;

    void update(threadmaxx::Engine& /*engine*/) override {}
    void onShutdown(threadmaxx::Engine& engine) override;

    threadmaxx::LoaderStats stats() const noexcept override;

    /// Create the fallback 1x1 white texture. Idempotent — a second call
    /// replaces the previous resident copy.
    threadmaxx::ResourceHandle<Texture> createFallback(threadmaxx::Engine& engine);

    /// See @ref MeshLoader::releaseGpuResources — called by the renderer
    /// before the Vulkan device is destroyed.
    void releaseGpuResources() noexcept;

private:
    VulkanContext& ctx_;
    threadmaxx::ResourceHandle<Texture> fallback_;
    std::vector<Texture>                ownedTextures_;
    std::atomic<std::uint64_t>          resident_ = 0;
};

} // namespace threadmaxx_vk
