#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>

struct GLFWwindow;

namespace threadmaxx_vk {

/// Vulkan 1.3 instance + device + queues. Bring-up only: this class
/// owns nothing but the raw handles and the optional debug messenger.
/// Anything frame-scoped (swapchain, semaphores, command pools) lives
/// in @ref VulkanFrameRing / @ref VulkanSwapchain.
///
/// Requirements: VK_KHR_dynamic_rendering (core in 1.3), timeline
/// semaphores (core in 1.2), synchronization2 (core in 1.3). All
/// three come for free on a real 1.3 device.
class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    bool init(GLFWwindow* window, bool enableValidation);
    void shutdown() noexcept;

    VkInstance       instance()       const noexcept { return instance_; }
    VkPhysicalDevice physicalDevice() const noexcept { return physicalDevice_; }
    VkDevice         device()         const noexcept { return device_; }
    VkSurfaceKHR     surface()        const noexcept { return surface_; }

    VkQueue       graphicsQueue()      const noexcept { return graphicsQueue_; }
    std::uint32_t graphicsQueueIndex() const noexcept { return graphicsQueueIndex_; }
    VkQueue       presentQueue()       const noexcept { return presentQueue_; }
    std::uint32_t presentQueueIndex()  const noexcept { return presentQueueIndex_; }

    /// Pick a memory type index that matches the requested type bits and
    /// property flags. Aborts if no matching type exists.
    std::uint32_t findMemoryType(std::uint32_t typeBits,
                                 VkMemoryPropertyFlags properties) const;

private:
    VkInstance               instance_       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR             surface_        = VK_NULL_HANDLE;
    VkPhysicalDevice         physicalDevice_ = VK_NULL_HANDLE;
    VkDevice                 device_         = VK_NULL_HANDLE;

    VkQueue       graphicsQueue_      = VK_NULL_HANDLE;
    std::uint32_t graphicsQueueIndex_ = 0;
    VkQueue       presentQueue_       = VK_NULL_HANDLE;
    std::uint32_t presentQueueIndex_  = 0;
};

} // namespace threadmaxx_vk
