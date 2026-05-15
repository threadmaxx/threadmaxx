#pragma once

#include "VulkanContext.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

namespace threadmaxx_vk {

/// One frame-in-flight slot: command pool + primary buffer + the per-image
/// binary semaphores used by the swapchain.
struct FrameSlot {
    VkCommandPool   cmdPool       = VK_NULL_HANDLE;
    VkCommandBuffer cmd           = VK_NULL_HANDLE;
    /// Signaled by `vkAcquireNextImageKHR`, waited on by the graphics
    /// submit's COLOR_ATTACHMENT_OUTPUT stage. Per-frame-slot is fine:
    /// the slot's reuse fence (timeline value) ensures the semaphore is
    /// idle before re-acquire.
    VkSemaphore     imageAvailable = VK_NULL_HANDLE;
    /// Timeline value this slot expects on the global graphics timeline
    /// before it can be reused. Bumped at every submit.
    std::uint64_t   waitTimelineValue = 0;
};

/// Owns the frame-in-flight slots + the global graphics timeline semaphore.
class VulkanFrameRing {
public:
    VulkanFrameRing() = default;
    ~VulkanFrameRing();

    VulkanFrameRing(const VulkanFrameRing&) = delete;
    VulkanFrameRing& operator=(const VulkanFrameRing&) = delete;

    bool create(VulkanContext& ctx, std::uint32_t framesInFlight);
    void destroy(VulkanContext& ctx) noexcept;

    std::uint32_t framesInFlight() const noexcept {
        return static_cast<std::uint32_t>(slots_.size());
    }
    FrameSlot&    slot(std::uint32_t i)        noexcept { return slots_[i]; }
    const FrameSlot& slot(std::uint32_t i) const noexcept { return slots_[i]; }

    VkSemaphore     timeline()    const noexcept { return timeline_; }
    std::uint64_t   timelineHead() const noexcept { return timelineHead_; }
    std::uint64_t   bumpTimeline()      noexcept { return ++timelineHead_; }

private:
    std::vector<FrameSlot> slots_;
    VkSemaphore   timeline_     = VK_NULL_HANDLE;
    std::uint64_t timelineHead_ = 0;
};

} // namespace threadmaxx_vk
