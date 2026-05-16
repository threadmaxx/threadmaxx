#include <threadmaxx_vk/VulkanRenderer.hpp>
#include <threadmaxx_vk/Mesh.hpp>

#include "VkUtil.hpp"
#include "VulkanContext.hpp"
#include "VulkanFrameRing.hpp"
#include "VulkanPipelines.hpp"
#include "VulkanSwapchain.hpp"
#include "MeshLoader.hpp"
#include "TextureLoader.hpp"
#include "ShaderLoader.hpp"

#include <threadmaxx/Engine.hpp>
#include <threadmaxx/render/InstanceBufferLayout.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace threadmaxx_vk {

namespace {

// Convert 0xAABBGGRR (the channel convention DebugLine/DebugPoint use) into
// 4 floats in [0, 1].
inline void unpackRGBA(std::uint32_t rgba, float out[4]) noexcept {
    out[0] = static_cast<float>((rgba >>  0) & 0xFFu) / 255.0f;
    out[1] = static_cast<float>((rgba >>  8) & 0xFFu) / 255.0f;
    out[2] = static_cast<float>((rgba >> 16) & 0xFFu) / 255.0f;
    out[3] = static_cast<float>((rgba >> 24) & 0xFFu) / 255.0f;
}

void imageBarrier(VkCommandBuffer cmd,
                  VkImage image,
                  VkImageAspectFlags aspect,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                  VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                  VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier2 b = {};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask  = srcStage;
    b.srcAccessMask = srcAccess;
    b.dstStageMask  = dstStage;
    b.dstAccessMask = dstAccess;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = aspect;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;

    VkDependencyInfo di = {};
    di.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    di.imageMemoryBarrierCount = 1;
    di.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &di);
}

} // namespace

struct VulkanRenderer::Impl {
    threadmaxx::Engine* engine = nullptr;
    GLFWwindow*         window = nullptr;
    Config              cfg;

    VulkanContext   ctx;
    VulkanSwapchain swapchain;
    VulkanFrameRing frames;
    VulkanPipelines pipes;

    // Non-owning: the engine owns each loader's unique_ptr.
    MeshLoader*    meshLoader    = nullptr;
    TextureLoader* textureLoader = nullptr;
    ShaderLoader*  shaderLoader  = nullptr;

    // Cached cube handle — every instance batches against this for v1.
    threadmaxx::ResourceHandle<Mesh> cubeHandle;

    // Per-frame instance buffer (host-visible). Resized on demand.
    struct PerFrame {
        VkBuffer       instanceBuffer = VK_NULL_HANDLE;
        VkDeviceMemory instanceMemory = VK_NULL_HANDLE;
        VkDeviceSize   instanceCapacity = 0;

        VkBuffer       debugLineBuffer = VK_NULL_HANDLE;
        VkDeviceMemory debugLineMemory = VK_NULL_HANDLE;
        VkDeviceSize   debugLineCapacity = 0;
        VkBuffer       debugPointBuffer = VK_NULL_HANDLE;
        VkDeviceMemory debugPointMemory = VK_NULL_HANDLE;
        VkDeviceSize   debugPointCapacity = 0;
    };
    std::vector<PerFrame> perFrame;

    std::uint32_t frameIndex   = 0;
    bool          swapchainOk  = false;
    bool          resizePending = false;
    std::uint32_t pendingWidth  = 0;
    std::uint32_t pendingHeight = 0;

    std::atomic<std::uint64_t> framesSubmitted = 0;

    void ensureBuffer(PerFrame& pf, VkDeviceSize bytes, VkBufferUsageFlags usage,
                      VkBuffer& outBuffer, VkDeviceMemory& outMemory,
                      VkDeviceSize& outCapacity);
    void destroyPerFrame() noexcept;
    bool recreateSwapchain();
    void recordFrame(VkCommandBuffer cmd, std::uint32_t imageIndex,
                     const threadmaxx::RenderFrame& frame, PerFrame& pf);
    // §3.11.2 batch D2 — per-camera slice into the pre-packed
    // instance buffer. Offset is bytes from the buffer base; count is
    // the number of `InstanceLayoutEntry` records for that camera.
    struct CameraSlice {
        VkDeviceSize  offsetBytes  = 0;
        std::uint32_t instanceCount = 0;
    };
    void recordCamera(VkCommandBuffer cmd, const threadmaxx::Camera& cam,
                      std::uint32_t cameraIndex,
                      const threadmaxx::RenderFrame& frame,
                      PerFrame& pf,
                      const CameraSlice& slice);
};

// ---------------------------------------------------------------------------
// Construction / lifecycle
// ---------------------------------------------------------------------------

VulkanRenderer::VulkanRenderer(threadmaxx::Engine* engine,
                               GLFWwindow*         window,
                               Config              cfg)
    : impl_(std::make_unique<Impl>()) {
    impl_->engine = engine;
    impl_->window = window;
    impl_->cfg = cfg;
}

VulkanRenderer::~VulkanRenderer() = default;

bool VulkanRenderer::initialize() {
    if (!impl_->ctx.init(impl_->window, impl_->cfg.enableValidation)) {
        return false;
    }
    if (!impl_->swapchain.create(impl_->ctx, impl_->window,
                                 impl_->cfg.width, impl_->cfg.height)) {
        std::fprintf(stderr,
            "[vulkan_renderer] swapchain creation failed (window may be minimized)\n");
        return false;
    }
    if (!impl_->frames.create(impl_->ctx, impl_->cfg.framesInFlight)) {
        return false;
    }
    if (!impl_->pipes.create(impl_->ctx,
                             impl_->swapchain.colorFormat(),
                             impl_->swapchain.depthFormat())) {
        return false;
    }

    impl_->perFrame.resize(impl_->cfg.framesInFlight);

    // ---- Asset loaders --------------------------------------------------
    {
        auto mesh = std::make_unique<MeshLoader>(impl_->ctx);
        impl_->meshLoader = mesh.get();
        impl_->engine->addResourceLoader(std::move(mesh));
    }
    {
        auto tex = std::make_unique<TextureLoader>(impl_->ctx);
        impl_->textureLoader = tex.get();
        impl_->engine->addResourceLoader(std::move(tex));
    }
    {
        auto sh = std::make_unique<ShaderLoader>(impl_->ctx);
        impl_->shaderLoader = sh.get();
        impl_->engine->addResourceLoader(std::move(sh));
    }

    // Stand up the fallback unit-cube mesh that every Milestone-1
    // `RenderTag` entity batches against until batch 10 plugs in real
    // mesh assets.
    impl_->cubeHandle = impl_->meshLoader->createUnitCube(*impl_->engine);

    return true;
}

void VulkanRenderer::shutdown() {
    if (!impl_) return;

    if (impl_->ctx.device()) vkDeviceWaitIdle(impl_->ctx.device());

    // Release loader-owned GPU memory while the context is still live.
    // The engine's later onShutdown pass — which fires after IRenderer
    // shutdown — finds the device gone and would crash without this.
    if (impl_->meshLoader)    impl_->meshLoader->releaseGpuResources();
    if (impl_->textureLoader) impl_->textureLoader->releaseGpuResources();

    impl_->cubeHandle.reset();
    impl_->destroyPerFrame();
    impl_->pipes.destroy(impl_->ctx);
    impl_->frames.destroy(impl_->ctx);
    impl_->swapchain.destroy(impl_->ctx);
    impl_->ctx.shutdown();
}

void VulkanRenderer::onResize(std::uint32_t width, std::uint32_t height) {
    impl_->resizePending = true;
    impl_->pendingWidth = width;
    impl_->pendingHeight = height;
}

std::uint64_t VulkanRenderer::framesSubmitted() const noexcept {
    return impl_->framesSubmitted.load(std::memory_order_relaxed);
}

void VulkanRenderer::submitFrame(const threadmaxx::RenderFrame& frame) {
    if (impl_->resizePending) {
        if (!impl_->recreateSwapchain()) return;
        impl_->resizePending = false;
    }

    if (impl_->swapchain.extent().width == 0 ||
        impl_->swapchain.extent().height == 0) {
        return;
    }

    const std::uint32_t fi = impl_->frameIndex;
    auto& slot = impl_->frames.slot(fi);

    // Wait for this slot to be free.
    if (slot.waitTimelineValue > 0) {
        VkSemaphoreWaitInfo wi = {};
        wi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        VkSemaphore sem = impl_->frames.timeline();
        wi.semaphoreCount = 1;
        wi.pSemaphores = &sem;
        wi.pValues = &slot.waitTimelineValue;
        VK_CHECK(vkWaitSemaphores(impl_->ctx.device(), &wi, UINT64_MAX));
    }

    // Acquire the next swapchain image.
    std::uint32_t imageIndex = 0;
    VkResult acq = vkAcquireNextImageKHR(
        impl_->ctx.device(), impl_->swapchain.handle(), UINT64_MAX,
        slot.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
        impl_->resizePending = true;
        return;
    }
    if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
        std::fprintf(stderr, "[vulkan_renderer] vkAcquireNextImageKHR -> %s\n",
                     vkResultName(acq));
        return;
    }

    VK_CHECK(vkResetCommandBuffer(slot.cmd, 0));
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(slot.cmd, &bi));

    impl_->recordFrame(slot.cmd, imageIndex, frame, impl_->perFrame[fi]);

    VK_CHECK(vkEndCommandBuffer(slot.cmd));

    // Submit: wait on imageAvailable, signal renderFinished + bump timeline.
    const std::uint64_t signalValue = impl_->frames.bumpTimeline();
    slot.waitTimelineValue = signalValue;

    VkSemaphoreSubmitInfo waitSI = {};
    waitSI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitSI.semaphore = slot.imageAvailable;
    waitSI.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphore presentSem = impl_->swapchain.image(imageIndex).renderFinished;
    VkSemaphoreSubmitInfo signalSIs[2] = {};
    signalSIs[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalSIs[0].semaphore = presentSem;
    signalSIs[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    signalSIs[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalSIs[1].semaphore = impl_->frames.timeline();
    signalSIs[1].value = signalValue;
    signalSIs[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo cbSI = {};
    cbSI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cbSI.commandBuffer = slot.cmd;

    VkSubmitInfo2 submit = {};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &waitSI;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cbSI;
    submit.signalSemaphoreInfoCount = 2;
    submit.pSignalSemaphoreInfos = signalSIs;

    VK_CHECK(vkQueueSubmit2(impl_->ctx.graphicsQueue(), 1, &submit, VK_NULL_HANDLE));

    // Present.
    VkSwapchainKHR sc = impl_->swapchain.handle();
    VkPresentInfoKHR pi = {};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &presentSem;
    pi.swapchainCount = 1;
    pi.pSwapchains = &sc;
    pi.pImageIndices = &imageIndex;
    VkResult pr = vkQueuePresentKHR(impl_->ctx.presentQueue(), &pi);
    if (pr == VK_ERROR_OUT_OF_DATE_KHR || pr == VK_SUBOPTIMAL_KHR) {
        impl_->resizePending = true;
    } else if (pr != VK_SUCCESS) {
        std::fprintf(stderr, "[vulkan_renderer] vkQueuePresentKHR -> %s\n",
                     vkResultName(pr));
    }

    impl_->framesSubmitted.fetch_add(1, std::memory_order_relaxed);
    impl_->frameIndex = (fi + 1u) % impl_->frames.framesInFlight();
}

// ---------------------------------------------------------------------------
// Impl helpers
// ---------------------------------------------------------------------------

void VulkanRenderer::Impl::ensureBuffer(PerFrame& /*pf*/, VkDeviceSize bytes,
                                       VkBufferUsageFlags usage,
                                       VkBuffer& outBuffer,
                                       VkDeviceMemory& outMemory,
                                       VkDeviceSize& outCapacity) {
    if (bytes <= outCapacity && outBuffer != VK_NULL_HANDLE) return;
    if (outBuffer) {
        vkDestroyBuffer(ctx.device(), outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
    }
    if (outMemory) {
        vkFreeMemory(ctx.device(), outMemory, nullptr);
        outMemory = VK_NULL_HANDLE;
    }
    const VkDeviceSize newCap = std::max<VkDeviceSize>(bytes * 2, 4096);

    VkBufferCreateInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = newCap;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(ctx.device(), &bi, nullptr, &outBuffer));

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(ctx.device(), outBuffer, &mr);
    VkMemoryAllocateInfo ma = {};
    ma.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ma.allocationSize = mr.size;
    ma.memoryTypeIndex = ctx.findMemoryType(mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VK_CHECK(vkAllocateMemory(ctx.device(), &ma, nullptr, &outMemory));
    VK_CHECK(vkBindBufferMemory(ctx.device(), outBuffer, outMemory, 0));
    outCapacity = newCap;
}

void VulkanRenderer::Impl::destroyPerFrame() noexcept {
    for (auto& pf : perFrame) {
        if (pf.instanceBuffer)   vkDestroyBuffer(ctx.device(), pf.instanceBuffer,   nullptr);
        if (pf.instanceMemory)   vkFreeMemory  (ctx.device(), pf.instanceMemory,   nullptr);
        if (pf.debugLineBuffer)  vkDestroyBuffer(ctx.device(), pf.debugLineBuffer,  nullptr);
        if (pf.debugLineMemory)  vkFreeMemory  (ctx.device(), pf.debugLineMemory,  nullptr);
        if (pf.debugPointBuffer) vkDestroyBuffer(ctx.device(), pf.debugPointBuffer, nullptr);
        if (pf.debugPointMemory) vkFreeMemory  (ctx.device(), pf.debugPointMemory, nullptr);
        pf = {};
    }
    perFrame.clear();
}

bool VulkanRenderer::Impl::recreateSwapchain() {
    vkDeviceWaitIdle(ctx.device());
    swapchain.destroy(ctx);
    pipes.destroy(ctx);

    const std::uint32_t w = pendingWidth  ? pendingWidth  : cfg.width;
    const std::uint32_t h = pendingHeight ? pendingHeight : cfg.height;
    if (!swapchain.create(ctx, window, w, h)) {
        return false;
    }
    if (!pipes.create(ctx, swapchain.colorFormat(), swapchain.depthFormat())) {
        return false;
    }
    return true;
}

void VulkanRenderer::Impl::recordFrame(VkCommandBuffer cmd, std::uint32_t imageIndex,
                                       const threadmaxx::RenderFrame& frame,
                                       PerFrame& pf) {
    auto& image = swapchain.image(imageIndex);
    const VkExtent2D ext = swapchain.extent();

    // Color: UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL
    imageBarrier(cmd, image.color, VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    // Depth: UNDEFINED -> DEPTH_ATTACHMENT_OPTIMAL
    imageBarrier(cmd, swapchain.depthImage(), VK_IMAGE_ASPECT_DEPTH_BIT,
                 VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                 VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                 VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    VkRenderingAttachmentInfo colorAtt = {};
    colorAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAtt.imageView = image.colorView;
    colorAtt.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.clearValue.color = {{0.03f, 0.04f, 0.07f, 1.0f}};

    VkRenderingAttachmentInfo depthAtt = {};
    depthAtt.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depthAtt.imageView = swapchain.depthView();
    depthAtt.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo ri = {};
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea.extent = ext;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &colorAtt;
    ri.pDepthAttachment = &depthAtt;

    vkCmdBeginRendering(cmd, &ri);

    // Default viewport/scissor — per-camera draws set their own.
    VkViewport vp = {};
    vp.x = 0.0f;
    vp.y = static_cast<float>(ext.height);
    vp.width  = static_cast<float>(ext.width);
    vp.height = -static_cast<float>(ext.height);  // flip Y so Vulkan NDC matches GL convention
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &vp);
    VkRect2D sc = {{0, 0}, ext};
    vkCmdSetScissor(cmd, 0, 1, &sc);

    // ---- Multi-camera pass --------------------------------------------------
    if (frame.cameras.empty()) {
        // No cameras pushed by user systems → nothing to project. The
        // clear color above is the only visible output. This is the
        // smoke / first-tick state.
    } else {
        // §3.11.2 batch D2 — pre-pack every camera's instance slice
        // into one contiguous buffer BEFORE the camera loop starts,
        // so per-camera `ensureBuffer` calls can't destroy a buffer
        // that an earlier camera already bound. Each camera gets its
        // own `(offset, count)` window into the packed buffer; the
        // bind uses `pOffsets` so the shader's instance index 0 maps
        // to the camera's first entry.
        std::vector<threadmaxx::InstanceLayoutEntry> packed;
        std::vector<CameraSlice> slices(frame.cameras.size());
        const std::size_t cameraCount =
            std::min<std::size_t>(frame.cameras.size(), 32);
        for (std::size_t i = 0; i < cameraCount; ++i) {
            const std::uint32_t bit = (1u << i);
            const VkDeviceSize off = sizeof(threadmaxx::InstanceLayoutEntry) *
                                     packed.size();
            // Auto-instance lane (Milestone-1 contract): always visible.
            for (const auto& inst : frame.instances) {
                threadmaxx::DrawItem virt = {};
                virt.entity = inst.entity;
                virt.transform = inst.transform;
                virt.meshId = inst.meshId;
                virt.materialId = inst.materialId;
                virt.flags = inst.flags;
                if (inst.meshId < 0 || inst.materialId < 0) {
                    virt.materialOverride.params = {0.85f, 0.85f, 0.88f, 1.0f};
                } else {
                    virt.materialOverride.params = {1.0f, 1.0f, 1.0f, 1.0f};
                }
                packed.push_back(threadmaxx::packInstance(virt));
            }
            // Camera-mask-filtered opaque items.
            const auto opaqueItems = frame.drawItems[threadmaxx::passIndex(
                threadmaxx::RenderPass::Opaque)];
            for (const auto& di : opaqueItems) {
                if ((di.cameraMask & bit) == 0) continue;
                packed.push_back(threadmaxx::packInstance(di));
            }
            const std::size_t after = packed.size();
            slices[i].offsetBytes  = off;
            slices[i].instanceCount = static_cast<std::uint32_t>(
                after - off / sizeof(threadmaxx::InstanceLayoutEntry));
        }

        if (!packed.empty()) {
            const VkDeviceSize bytes =
                sizeof(threadmaxx::InstanceLayoutEntry) * packed.size();
            ensureBuffer(pf, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         pf.instanceBuffer, pf.instanceMemory,
                         pf.instanceCapacity);
            void* p = nullptr;
            VK_CHECK(vkMapMemory(ctx.device(), pf.instanceMemory, 0,
                                 bytes, 0, &p));
            std::memcpy(p, packed.data(), bytes);
            vkUnmapMemory(ctx.device(), pf.instanceMemory);
        }

        for (std::size_t i = 0; i < cameraCount; ++i) {
            recordCamera(cmd, frame.cameras[i],
                         static_cast<std::uint32_t>(i),
                         frame, pf, slices[i]);
        }
    }

    vkCmdEndRendering(cmd);

    // Color: COLOR_ATTACHMENT_OPTIMAL -> PRESENT_SRC_KHR
    imageBarrier(cmd, image.color, VK_IMAGE_ASPECT_COLOR_BIT,
                 VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

void VulkanRenderer::Impl::recordCamera(VkCommandBuffer cmd,
                                        const threadmaxx::Camera& cam,
                                        std::uint32_t cameraIndex,
                                        const threadmaxx::RenderFrame& frame,
                                        PerFrame& pf,
                                        const CameraSlice& slice) {
    // §3.11.2 batch D2 — set per-camera viewport + scissor from
    // `Camera::viewport`. Defaults to full-screen so pre-D2 cameras
    // behave bit-for-bit as before. Y is flipped here (height < 0)
    // so the engine's column-major view*proj output matches the
    // GL/OpenGL NDC convention the shaders expect.
    {
        const VkExtent2D ext = swapchain.extent();
        const float fw = static_cast<float>(ext.width);
        const float fh = static_cast<float>(ext.height);
        const float vx = cam.viewport.x      * fw;
        const float vy = cam.viewport.y      * fh;
        const float vw = cam.viewport.width  * fw;
        const float vh = cam.viewport.height * fh;
        VkViewport vp = {};
        vp.x = vx;
        vp.y = vy + vh;       // top of rect + flipped height
        vp.width  =  vw;
        vp.height = -vh;      // flip Y
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc = {};
        sc.offset.x = static_cast<std::int32_t>(vx);
        sc.offset.y = static_cast<std::int32_t>(vy);
        sc.extent.width  = static_cast<std::uint32_t>(vw);
        sc.extent.height = static_cast<std::uint32_t>(vh);
        vkCmdSetScissor(cmd, 0, 1, &sc);
    }

    // Compute view*proj column-major.
    float vp[16];
    {
        const auto& p = cam.projection;
        const auto& v = cam.view;
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                float sum = 0.0f;
                for (int k = 0; k < 4; ++k) {
                    sum += p[static_cast<std::size_t>(k * 4 + row)] *
                           v[static_cast<std::size_t>(col * 4 + k)];
                }
                vp[col * 4 + row] = sum;
            }
        }
    }

    (void)cameraIndex;

    // ---- Opaque pass --------------------------------------------------------
    // §3.11.2 batch D2 — instances were pre-packed in recordFrame;
    // we just bind the camera's slice via `pOffsets`. No per-camera
    // ensureBuffer that could invalidate already-bound buffers.
    if (slice.instanceCount > 0 && cubeHandle.valid()) {
        const Mesh* mesh = cubeHandle.get();
        if (mesh && mesh->gpuReady) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes.opaquePipe());

            OpaquePushConstants pc = {};
            std::memcpy(pc.viewProj, vp, sizeof(vp));
            // Down-and-to-the-right directional light.
            pc.lightDir[0] = -0.3f;
            pc.lightDir[1] = -1.0f;
            pc.lightDir[2] = -0.2f;
            pc.cameraPos[0] = cam.position.x;
            pc.cameraPos[1] = cam.position.y;
            pc.cameraPos[2] = cam.position.z;
            vkCmdPushConstants(cmd, pipes.opaqueLayout(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), &pc);

            VkBuffer vbufs[2] = {mesh->vertexBuffer, pf.instanceBuffer};
            // §3.11.2 batch D2 — binding 1 (per-instance) uses this
            // camera's slice offset. The shader's gl_InstanceIndex 0
            // maps to the first record of the slice.
            VkDeviceSize voffs[2] = {0, slice.offsetBytes};
            vkCmdBindVertexBuffers(cmd, 0, 2, vbufs, voffs);
            vkCmdBindIndexBuffer(cmd, mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT16);

            vkCmdDrawIndexed(cmd, mesh->indexCount,
                             slice.instanceCount,
                             0, 0, 0);
        }
    }

    // ---- Debug lines / points (single shared pass) --------------------------
    if (!frame.debugLines.empty()) {
        std::vector<float> verts;
        verts.reserve(frame.debugLines.size() * 14);  // 2 endpoints × (3 pos + 4 col)
        for (const auto& l : frame.debugLines) {
            float col[4];
            unpackRGBA(l.colorRGBA, col);
            verts.insert(verts.end(),
                {l.a.x, l.a.y, l.a.z, col[0], col[1], col[2], col[3]});
            verts.insert(verts.end(),
                {l.b.x, l.b.y, l.b.z, col[0], col[1], col[2], col[3]});
        }
        const VkDeviceSize bytes = verts.size() * sizeof(float);
        ensureBuffer(pf, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     pf.debugLineBuffer, pf.debugLineMemory,
                     pf.debugLineCapacity);
        void* p = nullptr;
        VK_CHECK(vkMapMemory(ctx.device(), pf.debugLineMemory, 0, bytes, 0, &p));
        std::memcpy(p, verts.data(), bytes);
        vkUnmapMemory(ctx.device(), pf.debugLineMemory);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes.debugLinePipe());
        DebugPushConstants pc = {};
        std::memcpy(pc.viewProj, vp, sizeof(vp));
        vkCmdPushConstants(cmd, pipes.debugLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &pf.debugLineBuffer, &off);
        vkCmdDraw(cmd, static_cast<std::uint32_t>(frame.debugLines.size()) * 2, 1, 0, 0);
    }

    if (!frame.debugPoints.empty()) {
        std::vector<float> verts;
        verts.reserve(frame.debugPoints.size() * 8);
        for (const auto& pt : frame.debugPoints) {
            float col[4];
            unpackRGBA(pt.colorRGBA, col);
            verts.insert(verts.end(),
                {pt.position.x, pt.position.y, pt.position.z,
                 col[0], col[1], col[2], col[3], pt.pixelSize});
        }
        const VkDeviceSize bytes = verts.size() * sizeof(float);
        ensureBuffer(pf, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     pf.debugPointBuffer, pf.debugPointMemory,
                     pf.debugPointCapacity);
        void* p = nullptr;
        VK_CHECK(vkMapMemory(ctx.device(), pf.debugPointMemory, 0, bytes, 0, &p));
        std::memcpy(p, verts.data(), bytes);
        vkUnmapMemory(ctx.device(), pf.debugPointMemory);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes.debugPointPipe());
        DebugPushConstants pc = {};
        std::memcpy(pc.viewProj, vp, sizeof(vp));
        vkCmdPushConstants(cmd, pipes.debugLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &pf.debugPointBuffer, &off);
        vkCmdDraw(cmd, static_cast<std::uint32_t>(frame.debugPoints.size()), 1, 0, 0);
    }
}

} // namespace threadmaxx_vk
