#pragma once

#include "VulkanContext.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>

namespace threadmaxx_vk {

/// Push-constant layout for the opaque pipeline. Matches the shader.
struct OpaquePushConstants {
    float viewProj[16];
    float lightDir[4];   // xyz + 0
    float cameraPos[4];  // xyz + 0
};

/// Push-constant layout for the debug pipelines (lines / points).
struct DebugPushConstants {
    float viewProj[16];
};

/// Builds and owns the three pipelines + their layouts. Pipelines depend
/// on the swapchain color/depth formats (via dynamic-rendering's pipeline
/// rendering info), so they're rebuilt whenever the swapchain is.
class VulkanPipelines {
public:
    VulkanPipelines() = default;
    ~VulkanPipelines();

    VulkanPipelines(const VulkanPipelines&) = delete;
    VulkanPipelines& operator=(const VulkanPipelines&) = delete;

    bool create(VulkanContext& ctx,
                VkFormat colorFormat,
                VkFormat depthFormat);
    void destroy(VulkanContext& ctx) noexcept;

    VkPipelineLayout opaqueLayout() const noexcept { return opaqueLayout_; }
    VkPipeline       opaquePipe()   const noexcept { return opaquePipe_; }

    VkPipelineLayout debugLayout()    const noexcept { return debugLayout_; }
    VkPipeline       debugLinePipe()  const noexcept { return debugLinePipe_; }
    VkPipeline       debugPointPipe() const noexcept { return debugPointPipe_; }

private:
    static VkShaderModule makeShader(VkDevice device, std::span<const std::uint32_t> spirv);

    VkPipelineLayout opaqueLayout_     = VK_NULL_HANDLE;
    VkPipeline       opaquePipe_       = VK_NULL_HANDLE;

    VkPipelineLayout debugLayout_      = VK_NULL_HANDLE;
    VkPipeline       debugLinePipe_    = VK_NULL_HANDLE;
    VkPipeline       debugPointPipe_   = VK_NULL_HANDLE;
};

} // namespace threadmaxx_vk
