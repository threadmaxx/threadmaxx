#pragma once

#include "VulkanContext.hpp"

#include <threadmaxx/Resource.hpp>
#include <threadmaxx_vk/Mesh.hpp>  // Texture POD lives here

#include <atomic>
#include <cstdint>
#include <span>
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

    /// M2.8 — upload an RGBA8 (R, G, B, A; 4 bytes/pixel) buffer to a
    /// device-local, OPTIMAL-tiling, SAMPLED_BIT image and return a
    /// refcounted registry handle. Blocks on a one-shot command buffer
    /// + fence so the texture is GPU-resident on return. Returns an
    /// empty handle on input validation failure (mismatched span size
    /// or zero extent).
    threadmaxx::ResourceHandle<Texture> createFromRgba(
        threadmaxx::Engine&             engine,
        std::uint32_t                   width,
        std::uint32_t                   height,
        std::span<const std::uint8_t>   rgba);

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
