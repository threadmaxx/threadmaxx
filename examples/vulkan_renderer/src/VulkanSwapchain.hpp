#pragma once

#include "VulkanContext.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

struct GLFWwindow;

namespace threadmaxx_vk {

/// Vulkan swapchain + per-image color/depth attachments. Pairs with
/// @ref VulkanContext. Re-creatable on @ref VulkanRenderer::onResize.
///
/// All images use VK_KHR_dynamic_rendering's pipeline-barrier idiom; no
/// VkRenderPass / VkFramebuffer objects are kept.
class VulkanSwapchain {
public:
    struct Image {
        VkImage     color           = VK_NULL_HANDLE;
        VkImageView colorView       = VK_NULL_HANDLE;
        /// Signaled by the graphics submit that wrote to this image,
        /// waited on by the present that consumes it. Per-image so the
        /// presented semaphore is observably idle by the time the same
        /// image is re-acquired (vs. a per-frame-slot semaphore which
        /// validation flags when frames-in-flight < image count).
        VkSemaphore renderFinished  = VK_NULL_HANDLE;
    };

    VulkanSwapchain() = default;
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    bool create(VulkanContext& ctx, GLFWwindow* window,
                std::uint32_t desiredWidth, std::uint32_t desiredHeight);
    void destroy(VulkanContext& ctx) noexcept;

    VkSwapchainKHR  handle()  const noexcept { return swapchain_; }
    VkFormat        colorFormat() const noexcept { return colorFormat_; }
    VkFormat        depthFormat() const noexcept { return depthFormat_; }
    VkExtent2D      extent()  const noexcept { return extent_; }
    std::uint32_t   imageCount() const noexcept {
        return static_cast<std::uint32_t>(images_.size());
    }
    const Image&    image(std::uint32_t i) const noexcept { return images_[i]; }

    VkImage         depthImage() const noexcept { return depthImage_; }
    VkImageView     depthView()  const noexcept { return depthView_; }

private:
    void createDepth(VulkanContext& ctx);
    void destroyDepth(VulkanContext& ctx) noexcept;

    VkSwapchainKHR     swapchain_   = VK_NULL_HANDLE;
    VkFormat           colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat           depthFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D         extent_      = {0, 0};
    std::vector<Image> images_;

    VkImage        depthImage_  = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView    depthView_   = VK_NULL_HANDLE;
};

} // namespace threadmaxx_vk
