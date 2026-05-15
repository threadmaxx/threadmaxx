#include "VulkanSwapchain.hpp"

#include "VkUtil.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <vector>

namespace threadmaxx_vk {

namespace {

VkSurfaceFormatKHR pickSurfaceFormat(VkPhysicalDevice dev, VkSurfaceKHR surf) {
    std::uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surf, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surf, &n, formats.data());

    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM,
                                                VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
                           : formats[0];
}

VkPresentModeKHR pickPresentMode(VkPhysicalDevice dev, VkSurfaceKHR surf) {
    std::uint32_t n = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surf, &n, nullptr);
    std::vector<VkPresentModeKHR> modes(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surf, &n, modes.data());
    for (auto m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D pickExtent(const VkSurfaceCapabilitiesKHR& caps,
                      GLFWwindow* window,
                      std::uint32_t hintW, std::uint32_t hintH) {
    if (caps.currentExtent.width != UINT32_MAX) {
        return caps.currentExtent;
    }
    int fbW = static_cast<int>(hintW);
    int fbH = static_cast<int>(hintH);
    if (window) {
        glfwGetFramebufferSize(window, &fbW, &fbH);
    }
    VkExtent2D e = {static_cast<std::uint32_t>(fbW),
                    static_cast<std::uint32_t>(fbH)};
    e.width  = std::clamp(e.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    e.height = std::clamp(e.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return e;
}

} // namespace

VulkanSwapchain::~VulkanSwapchain() = default;

bool VulkanSwapchain::create(VulkanContext& ctx, GLFWwindow* window,
                             std::uint32_t desiredWidth,
                             std::uint32_t desiredHeight) {
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        ctx.physicalDevice(), ctx.surface(), &caps));

    const VkSurfaceFormatKHR sf = pickSurfaceFormat(ctx.physicalDevice(), ctx.surface());
    const VkPresentModeKHR   pm = pickPresentMode(ctx.physicalDevice(), ctx.surface());
    extent_ = pickExtent(caps, window, desiredWidth, desiredHeight);
    if (extent_.width == 0 || extent_.height == 0) {
        // Minimized window; skip creation and let the caller try again.
        swapchain_ = VK_NULL_HANDLE;
        return false;
    }
    colorFormat_ = sf.format;

    std::uint32_t desiredImages = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && desiredImages > caps.maxImageCount) {
        desiredImages = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR sc = {};
    sc.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sc.surface = ctx.surface();
    sc.minImageCount = desiredImages;
    sc.imageFormat = sf.format;
    sc.imageColorSpace = sf.colorSpace;
    sc.imageExtent = extent_;
    sc.imageArrayLayers = 1;
    sc.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    const std::uint32_t queueFamilies[2] = {
        ctx.graphicsQueueIndex(), ctx.presentQueueIndex()
    };
    if (ctx.graphicsQueueIndex() != ctx.presentQueueIndex()) {
        sc.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        sc.queueFamilyIndexCount = 2;
        sc.pQueueFamilyIndices = queueFamilies;
    } else {
        sc.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    sc.preTransform = caps.currentTransform;
    sc.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sc.presentMode = pm;
    sc.clipped = VK_TRUE;
    sc.oldSwapchain = VK_NULL_HANDLE;

    VK_CHECK(vkCreateSwapchainKHR(ctx.device(), &sc, nullptr, &swapchain_));

    std::uint32_t imgCount = 0;
    vkGetSwapchainImagesKHR(ctx.device(), swapchain_, &imgCount, nullptr);
    std::vector<VkImage> raw(imgCount);
    vkGetSwapchainImagesKHR(ctx.device(), swapchain_, &imgCount, raw.data());

    images_.clear();
    images_.reserve(imgCount);
    for (auto img : raw) {
        Image entry;
        entry.color = img;

        VkImageViewCreateInfo iv = {};
        iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv.image = img;
        iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
        iv.format = colorFormat_;
        iv.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                         VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
        iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        iv.subresourceRange.levelCount = 1;
        iv.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(ctx.device(), &iv, nullptr, &entry.colorView));

        VkSemaphoreCreateInfo sci = {};
        sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(ctx.device(), &sci, nullptr, &entry.renderFinished));

        images_.push_back(entry);
    }

    createDepth(ctx);
    return true;
}

void VulkanSwapchain::destroy(VulkanContext& ctx) noexcept {
    destroyDepth(ctx);
    for (auto& e : images_) {
        if (e.renderFinished) vkDestroySemaphore(ctx.device(), e.renderFinished, nullptr);
        if (e.colorView)      vkDestroyImageView(ctx.device(), e.colorView, nullptr);
    }
    images_.clear();
    if (swapchain_) {
        vkDestroySwapchainKHR(ctx.device(), swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    colorFormat_ = VK_FORMAT_UNDEFINED;
    depthFormat_ = VK_FORMAT_UNDEFINED;
    extent_ = {0, 0};
}

void VulkanSwapchain::createDepth(VulkanContext& ctx) {
    // Prefer D32_SFLOAT — universally supported in 1.3 as a depth attachment.
    depthFormat_ = VK_FORMAT_D32_SFLOAT;

    VkImageCreateInfo ic = {};
    ic.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ic.imageType = VK_IMAGE_TYPE_2D;
    ic.format = depthFormat_;
    ic.extent = {extent_.width, extent_.height, 1};
    ic.mipLevels = 1;
    ic.arrayLayers = 1;
    ic.samples = VK_SAMPLE_COUNT_1_BIT;
    ic.tiling = VK_IMAGE_TILING_OPTIMAL;
    ic.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(ctx.device(), &ic, nullptr, &depthImage_));

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(ctx.device(), depthImage_, &mr);
    VkMemoryAllocateInfo ma = {};
    ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = ctx.findMemoryType(mr.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(ctx.device(), &ma, nullptr, &depthMemory_));
    VK_CHECK(vkBindImageMemory(ctx.device(), depthImage_, depthMemory_, 0));

    VkImageViewCreateInfo iv = {};
    iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    iv.image = depthImage_;
    iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    iv.format = depthFormat_;
    iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    iv.subresourceRange.levelCount = 1;
    iv.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(ctx.device(), &iv, nullptr, &depthView_));
}

void VulkanSwapchain::destroyDepth(VulkanContext& ctx) noexcept {
    if (depthView_)   { vkDestroyImageView(ctx.device(), depthView_, nullptr); depthView_ = VK_NULL_HANDLE; }
    if (depthImage_)  { vkDestroyImage(ctx.device(), depthImage_, nullptr);    depthImage_ = VK_NULL_HANDLE; }
    if (depthMemory_) { vkFreeMemory(ctx.device(), depthMemory_, nullptr);     depthMemory_ = VK_NULL_HANDLE; }
}

} // namespace threadmaxx_vk
