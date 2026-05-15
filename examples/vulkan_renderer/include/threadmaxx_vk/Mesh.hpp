#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace threadmaxx_vk {

/// CPU-side mesh handle resolved from a @ref ResourceId<Mesh>. Owns GPU
/// buffers via raw VkBuffer + VkDeviceMemory pairs. Loaders create these;
/// the renderer borrows during draw.
struct Mesh {
    VkBuffer       vertexBuffer       = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory       = VK_NULL_HANDLE;
    VkBuffer       indexBuffer        = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory        = VK_NULL_HANDLE;
    std::uint32_t  indexCount         = 0;
    /// Set when the loader has freed the GPU memory but kept the handle
    /// slot alive (e.g. asset eviction). The renderer skips drawing.
    bool           gpuReady           = false;
};

/// CPU-side texture handle. v1 stores nothing; reserved for batch 10.
struct Texture {
    VkImage        image  = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView    view   = VK_NULL_HANDLE;
    VkSampler      sampler = VK_NULL_HANDLE;
    std::uint32_t  width  = 0;
    std::uint32_t  height = 0;
};

/// CPU-side shader handle. Carries the raw SPIR-V bytecode plus an
/// optional VkShaderModule cache populated by the renderer when first
/// referenced. Hot reload swaps both atomically through the
/// @ref AssetReloaded event.
struct Shader {
    /// Owned bytecode. Empty after hot-reload-to-deleted.
    std::vector<std::uint32_t> spirv;
};

} // namespace threadmaxx_vk
