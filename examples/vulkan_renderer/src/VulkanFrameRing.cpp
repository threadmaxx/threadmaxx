#include "VulkanFrameRing.hpp"

#include "VkUtil.hpp"

namespace threadmaxx_vk {

VulkanFrameRing::~VulkanFrameRing() = default;

bool VulkanFrameRing::create(VulkanContext& ctx, std::uint32_t framesInFlight) {
    slots_.resize(framesInFlight);

    for (auto& s : slots_) {
        VkCommandPoolCreateInfo pc = {};
        pc.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pc.queueFamilyIndex = ctx.graphicsQueueIndex();
        pc.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK(vkCreateCommandPool(ctx.device(), &pc, nullptr, &s.cmdPool));

        VkCommandBufferAllocateInfo ai = {};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = s.cmdPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(ctx.device(), &ai, &s.cmd));

        VkSemaphoreCreateInfo sci = {};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(ctx.device(), &sci, nullptr, &s.imageAvailable));
    }

    VkSemaphoreTypeCreateInfo tt = {};
    tt.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    tt.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    tt.initialValue = 0;
    VkSemaphoreCreateInfo sci = {};
    sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    sci.pNext = &tt;
    VK_CHECK(vkCreateSemaphore(ctx.device(), &sci, nullptr, &timeline_));
    timelineHead_ = 0;
    return true;
}

void VulkanFrameRing::destroy(VulkanContext& ctx) noexcept {
    for (auto& s : slots_) {
        if (s.imageAvailable) vkDestroySemaphore(ctx.device(), s.imageAvailable, nullptr);
        if (s.cmdPool)        vkDestroyCommandPool(ctx.device(), s.cmdPool, nullptr);
        s = {};
    }
    slots_.clear();
    if (timeline_) {
        vkDestroySemaphore(ctx.device(), timeline_, nullptr);
        timeline_ = VK_NULL_HANDLE;
    }
    timelineHead_ = 0;
}

} // namespace threadmaxx_vk
