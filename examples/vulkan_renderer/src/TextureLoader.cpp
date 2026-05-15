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
