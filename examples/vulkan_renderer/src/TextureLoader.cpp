#include "TextureLoader.hpp"

#include "VkUtil.hpp"

#include <threadmaxx/Engine.hpp>

#include <cstdint>
#include <cstring>

namespace threadmaxx_vk {

namespace {

void allocateImage(VulkanContext& ctx, std::uint32_t w, std::uint32_t h,
                   VkFormat format, VkImage& img, VkDeviceMemory& mem) {
    VkImageCreateInfo ic = {};
    ic.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ic.imageType = VK_IMAGE_TYPE_2D;
    ic.format = format;
    ic.extent = {w, h, 1};
    ic.mipLevels = 1;
    ic.arrayLayers = 1;
    ic.samples = VK_SAMPLE_COUNT_1_BIT;
    ic.tiling = VK_IMAGE_TILING_LINEAR;
    ic.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ic.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    VK_CHECK(vkCreateImage(ctx.device(), &ic, nullptr, &img));

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(ctx.device(), img, &mr);
    VkMemoryAllocateInfo ma = {};
    ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = ctx.findMemoryType(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(ctx.device(), &ma, nullptr, &mem));
    VK_CHECK(vkBindImageMemory(ctx.device(), img, mem, 0));
}

} // namespace

void TextureLoader::onShutdown(threadmaxx::Engine& /*engine*/) {
    fallback_.reset();
}

void TextureLoader::releaseGpuResources() noexcept {
    if (!ctx_.device()) return;
    for (auto& t : ownedTextures_) {
        if (t.sampler) vkDestroySampler  (ctx_.device(), t.sampler, nullptr);
        if (t.view)    vkDestroyImageView(ctx_.device(), t.view,    nullptr);
        if (t.image)   vkDestroyImage    (ctx_.device(), t.image,   nullptr);
        if (t.memory)  vkFreeMemory      (ctx_.device(), t.memory,  nullptr);
    }
    ownedTextures_.clear();
    resident_.store(0, std::memory_order_relaxed);
}

threadmaxx::LoaderStats TextureLoader::stats() const noexcept {
    threadmaxx::LoaderStats s;
    s.memoryFootprint = resident_.load(std::memory_order_relaxed);
    return s;
}

threadmaxx::ResourceHandle<Texture> TextureLoader::createFromRgba(
    threadmaxx::Engine&           engine,
    std::uint32_t                 width,
    std::uint32_t                 height,
    std::span<const std::uint8_t> rgba) {
    if (width == 0 || height == 0) return {};
    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(width) * height * 4u;
    if (rgba.size_bytes() != bytes) return {};

    const VkDevice device = ctx_.device();

    // ---- 1. Staging buffer ------------------------------------------------
    VkBuffer       staging       = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo bi = {};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = bytes;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateBuffer(device, &bi, nullptr, &staging));

        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(device, staging, &mr);
        VkMemoryAllocateInfo ma = {};
        ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ma.allocationSize = mr.size;
        ma.memoryTypeIndex = ctx_.findMemoryType(mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK_CHECK(vkAllocateMemory(device, &ma, nullptr, &stagingMemory));
        VK_CHECK(vkBindBufferMemory(device, staging, stagingMemory, 0));

        void* p = nullptr;
        VK_CHECK(vkMapMemory(device, stagingMemory, 0, bytes, 0, &p));
        std::memcpy(p, rgba.data(), bytes);
        vkUnmapMemory(device, stagingMemory);
    }

    // ---- 2. Device-local image (OPTIMAL tiling, sampled + xfer dst) ------
    Texture t;
    t.width  = width;
    t.height = height;
    {
        VkImageCreateInfo ic = {};
        ic.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ic.imageType = VK_IMAGE_TYPE_2D;
        ic.format = VK_FORMAT_R8G8B8A8_SRGB;
        ic.extent = {width, height, 1};
        ic.mipLevels = 1;
        ic.arrayLayers = 1;
        ic.samples = VK_SAMPLE_COUNT_1_BIT;
        ic.tiling = VK_IMAGE_TILING_OPTIMAL;
        ic.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VK_CHECK(vkCreateImage(device, &ic, nullptr, &t.image));

        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(device, t.image, &mr);
        VkMemoryAllocateInfo ma = {};
        ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ma.allocationSize = mr.size;
        ma.memoryTypeIndex = ctx_.findMemoryType(mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK(vkAllocateMemory(device, &ma, nullptr, &t.memory));
        VK_CHECK(vkBindImageMemory(device, t.image, t.memory, 0));
    }

    // ---- 3. One-shot command buffer: layout xfer, copy, layout sample ----
    VkCommandPool   cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd     = VK_NULL_HANDLE;
    {
        VkCommandPoolCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.queueFamilyIndex = ctx_.graphicsQueueIndex();
        ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        VK_CHECK(vkCreateCommandPool(device, &ci, nullptr, &cmdPool));

        VkCommandBufferAllocateInfo ai = {};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = cmdPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(device, &ai, &cmd));
    }

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

    auto layoutBarrier = [&](VkImageLayout oldL, VkImageLayout newL,
                             VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                             VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
        VkImageMemoryBarrier2 b = {};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask  = srcStage;
        b.srcAccessMask = srcAccess;
        b.dstStageMask  = dstStage;
        b.dstAccessMask = dstAccess;
        b.oldLayout = oldL;
        b.newLayout = newL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = t.image;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.layerCount = 1;
        VkDependencyInfo di = {};
        di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        di.imageMemoryBarrierCount = 1;
        di.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &di);
    };

    layoutBarrier(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,   0,
                  VK_PIPELINE_STAGE_2_COPY_BIT,          VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(cmd, staging, t.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    layoutBarrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COPY_BIT,             VK_ACCESS_2_TRANSFER_WRITE_BIT,
                  VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,  VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    VK_CHECK(vkEndCommandBuffer(cmd));

    // Submit + wait synchronously — texture upload is a setup-time
    // operation, not a per-frame hot path. Fence-then-destroy is
    // simpler than re-using the renderer's frame ring for a one-shot.
    VkFence fence = VK_NULL_HANDLE;
    {
        VkFenceCreateInfo fci = {};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VK_CHECK(vkCreateFence(device, &fci, nullptr, &fence));
    }
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(ctx_.graphicsQueue(), 1, &si, fence));
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));
    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, cmdPool, nullptr);  // frees the buffer too
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    // ---- 4. Image view + sampler -----------------------------------------
    VkImageViewCreateInfo iv = {};
    iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    iv.image = t.image;
    iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    iv.format = VK_FORMAT_R8G8B8A8_SRGB;
    iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    iv.subresourceRange.levelCount = 1;
    iv.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(device, &iv, nullptr, &t.view));

    VkSamplerCreateInfo sc = {};
    sc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sc.magFilter = VK_FILTER_LINEAR;
    sc.minFilter = VK_FILTER_LINEAR;
    sc.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sc.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sc.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    VK_CHECK(vkCreateSampler(device, &sc, nullptr, &t.sampler));

    ownedTextures_.push_back(t);
    resident_.fetch_add(bytes, std::memory_order_relaxed);
    return engine.resources().addRefCounted<Texture>(t);
}

bool TextureLoader::updateRgbaRegion(
    Texture*                      tex,
    std::uint32_t                 x,
    std::uint32_t                 y,
    std::uint32_t                 w,
    std::uint32_t                 h,
    std::span<const std::uint8_t> rgba) {
    if (!tex || tex->image == VK_NULL_HANDLE) return false;
    if (w == 0 || h == 0)                     return false;
    if (x + w > tex->width)                   return false;
    if (y + h > tex->height)                  return false;
    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(w) * static_cast<VkDeviceSize>(h) * 4u;
    if (rgba.size_bytes() != bytes) return false;

    const VkDevice device = ctx_.device();

    // ---- 1. Staging buffer (host-visible, throwaway) ---------------------
    VkBuffer       staging       = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    {
        VkBufferCreateInfo bi = {};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size  = bytes;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateBuffer(device, &bi, nullptr, &staging));

        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(device, staging, &mr);
        VkMemoryAllocateInfo ma = {};
        ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ma.allocationSize = mr.size;
        ma.memoryTypeIndex = ctx_.findMemoryType(mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK_CHECK(vkAllocateMemory(device, &ma, nullptr, &stagingMemory));
        VK_CHECK(vkBindBufferMemory(device, staging, stagingMemory, 0));

        void* p = nullptr;
        VK_CHECK(vkMapMemory(device, stagingMemory, 0, bytes, 0, &p));
        std::memcpy(p, rgba.data(), bytes);
        vkUnmapMemory(device, stagingMemory);
    }

    // ---- 2. One-shot cmd buffer + fence ----------------------------------
    VkCommandPool   cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd     = VK_NULL_HANDLE;
    {
        VkCommandPoolCreateInfo ci = {};
        ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.queueFamilyIndex = ctx_.graphicsQueueIndex();
        ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        VK_CHECK(vkCreateCommandPool(device, &ci, nullptr, &cmdPool));

        VkCommandBufferAllocateInfo ai = {};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = cmdPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(device, &ai, &cmd));
    }

    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

    auto barrier = [&](VkImageLayout oldL, VkImageLayout newL,
                       VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                       VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
        VkImageMemoryBarrier2 b = {};
        b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b.srcStageMask  = srcStage;
        b.srcAccessMask = srcAccess;
        b.dstStageMask  = dstStage;
        b.dstAccessMask = dstAccess;
        b.oldLayout = oldL;
        b.newLayout = newL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = tex->image;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.layerCount = 1;
        VkDependencyInfo di = {};
        di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        di.imageMemoryBarrierCount = 1;
        di.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &di);
    };

    // SHADER_READ_ONLY → TRANSFER_DST. srcStage = FRAGMENT_SHADER pins
    // ordering against any in-flight render-frame sampling: the GPU
    // doesn't start the transfer until the prior fragment shaders that
    // sample this image have finished. No `vkDeviceWaitIdle` needed.
    barrier(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT);

    VkBufferImageCopy region = {};
    region.bufferOffset = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {static_cast<std::int32_t>(x),
                          static_cast<std::int32_t>(y), 0};
    region.imageExtent = {w, h, 1};
    vkCmdCopyBufferToImage(cmd, staging, tex->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    barrier(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COPY_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkFence fence = VK_NULL_HANDLE;
    {
        VkFenceCreateInfo fci = {};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        VK_CHECK(vkCreateFence(device, &fci, nullptr, &fence));
    }
    VkSubmitInfo si = {};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers    = &cmd;
    VK_CHECK(vkQueueSubmit(ctx_.graphicsQueue(), 1, &si, fence));
    VK_CHECK(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX));

    vkDestroyFence(device, fence, nullptr);
    vkDestroyCommandPool(device, cmdPool, nullptr);  // frees cmd too
    vkDestroyBuffer(device, staging, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
    return true;
}

threadmaxx::ResourceHandle<Texture> TextureLoader::createFallback(threadmaxx::Engine& engine) {
    Texture t;
    t.width = 1;
    t.height = 1;

    allocateImage(ctx_, 1, 1, VK_FORMAT_R8G8B8A8_UNORM, t.image, t.memory);

    void* p = nullptr;
    VK_CHECK(vkMapMemory(ctx_.device(), t.memory, 0, 4, 0, &p));
    const std::uint32_t white = 0xFFFFFFFFu;
    std::memcpy(p, &white, 4);
    vkUnmapMemory(ctx_.device(), t.memory);

    VkImageViewCreateInfo iv = {};
    iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    iv.image = t.image;
    iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
    iv.format = VK_FORMAT_R8G8B8A8_UNORM;
    iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    iv.subresourceRange.levelCount = 1;
    iv.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(ctx_.device(), &iv, nullptr, &t.view));

    VkSamplerCreateInfo sc = {};
    sc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sc.magFilter = VK_FILTER_NEAREST;
    sc.minFilter = VK_FILTER_NEAREST;
    sc.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sc.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sc.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VK_CHECK(vkCreateSampler(ctx_.device(), &sc, nullptr, &t.sampler));

    ownedTextures_.push_back(t);
    resident_.fetch_add(4, std::memory_order_relaxed);

    auto handle = engine.resources().addRefCounted<Texture>(t);
    fallback_ = handle;
    return handle;
}

} // namespace threadmaxx_vk
