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
#include <threadmaxx/EventChannel.hpp>
#include <threadmaxx/Resource.hpp>
#include <threadmaxx/render/InstanceBufferLayout.hpp>

#include <typeindex>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
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
    // §3.11 batch 9b.2 — this is now the "default mesh" used for
    // `meshId == 0` (and any meshId out of range). Game code can swap
    // it via `setDefaultMesh` / `setDefaultMeshFromData`.
    threadmaxx::ResourceHandle<Mesh> cubeHandle;

    // §3.11 batch 9b.2b — additional mesh slots, addressed by
    // `meshId >= 1`. Slot index `(meshId - 1)`. Game code registers
    // via `VulkanRenderer::registerMesh*`. Cleared in shutdown
    // before the loader's `releaseGpuResources` runs.
    std::vector<threadmaxx::ResourceHandle<Mesh>> meshSlots;

    // §3.11.7b.5 batch 9b.4.b — skinned mesh slots, addressed by
    // `meshId >= 1` in the skinned namespace. DrawItems with
    // `skeletonId >= 0` route through this slot table instead of
    // the unskinned `meshSlots`. Same lifetime rules.
    std::vector<threadmaxx::ResourceHandle<Mesh>> skinnedMeshSlots;

    // §3.11.7b.5 batch 9b.4.b — bone matrix descriptor pool. The
    // per-frame descriptor set lives in `PerFrame::boneDescriptorSet`
    // below; this pool just owns the allocation.
    VkDescriptorPool boneDescriptorPool = VK_NULL_HANDLE;

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

        // §3.11.7b.5 batch 9b.4.b — bone matrix buffer (host-visible,
        // mapped). Growable like instanceBuffer; the descriptor set
        // is updated whenever the buffer's backing VkBuffer changes
        // (resize → new handle → re-write descriptor).
        VkBuffer        boneBuffer        = VK_NULL_HANDLE;
        VkDeviceMemory  boneMemory        = VK_NULL_HANDLE;
        VkDeviceSize    boneCapacity      = 0;
        VkDeviceSize    boneSize          = 0;   // last setBoneMatrices size
        VkDescriptorSet boneDescriptorSet = VK_NULL_HANDLE;

        // §3.11 batch 9b.2b — per-camera per-meshId scratch buckets
        // for the bucket-and-pack draw flow. Reset each call to
        // `recordFrame`; vector capacities persist across frames so
        // steady-state pays zero allocations after the first tick.
        // §3.11.7b.5 batch 9b.4.b — `scratchSkinned[bucket]` matches
        // the parallel `scratchMeshIds[bucket]` and `scratchBuckets[bucket]`
        // entries, encoding whether the bucket is for the skinned
        // pipeline. Same scratch lifetime.
        std::vector<std::int32_t>                                 scratchMeshIds;
        std::vector<std::vector<threadmaxx::InstanceLayoutEntry>> scratchBuckets;
        std::vector<bool>                                         scratchSkinned;

        // 2026-05-20 — debug line/point vertex assembly used to happen
        // inside recordCamera, allocating a fresh std::vector<float> per
        // camera per frame and calling verts.insert(end(), {init_list})
        // once per line endpoint (slow under high-line workloads — at
        // 1M lines this dominated submitFrame at ~118 ms / tick).
        // Now the assembly happens once in recordFrame (cameras share
        // the buffer; the contents don't depend on the camera) with the
        // backing storage living on PerFrame so capacity persists.
        std::vector<float>     debugLineVerts;
        std::vector<float>     debugPointVerts;
        std::uint32_t          debugLineCount  = 0;
        std::uint32_t          debugPointCount = 0;
    };
    std::vector<PerFrame> perFrame;

    std::uint32_t frameIndex   = 0;
    bool          swapchainOk  = false;
    bool          resizePending = false;
    std::uint32_t pendingWidth  = 0;
    std::uint32_t pendingHeight = 0;

    std::atomic<std::uint64_t> framesSubmitted = 0;

    // 2026-05-20 — lightweight per-phase timing, summed across N frames
    // and printed every kProfilePeriod frames. Throwaway diagnostic
    // (active when env var THREADMAXX_VK_PROFILE=1 is set at runtime).
    // Zero overhead in the disabled path — one cmov per submit branch.
    bool             profileEnabled    = false;
    std::uint64_t    profileFrameCount = 0;
    std::uint64_t    profileSumWaitNs    = 0;
    std::uint64_t    profileSumAcquireNs = 0;
    std::uint64_t    profileSumRecordNs  = 0;
    std::uint64_t    profileSumSubmitNs  = 0;
    std::uint64_t    profileSumPresentNs = 0;
    std::uint64_t    profileSumPackNs    = 0;
    std::uint64_t    profileSumDrawCalls = 0;
    std::uint64_t    profileSumInstances = 0;
    static constexpr std::uint64_t kProfilePeriod = 120;

    // §3.11 batch 9b.3 — auto-detached subscription to the engine's
    // `AssetReloaded` channel. Filled in `initialize()`; the
    // `Subscription` dtor removes the listener in `~Impl`.
    threadmaxx::Subscription reloadSub;

    // §3.11.7b.5 batch 9b.4.b — helpers.
    /// Ensure the bone descriptor pool + per-frame descriptor sets
    /// exist. Idempotent; called once in `initialize()` after
    /// `pipes.create` so the bone set layout is available.
    void createBoneDescriptorResources();
    /// Update the descriptor set for frame slot `fi` to point at
    /// that frame's bone buffer (whole range). Called whenever the
    /// bone buffer is (re)created via `ensureBuffer`.
    void updateBoneDescriptor(std::uint32_t fi, PerFrame& pf);
    /// Tear down the bone descriptor pool. The per-frame bone
    /// buffers themselves are freed by `destroyPerFrame`.
    void destroyBoneDescriptorResources() noexcept;

    void ensureBuffer(PerFrame& pf, VkDeviceSize bytes, VkBufferUsageFlags usage,
                      VkBuffer& outBuffer, VkDeviceMemory& outMemory,
                      VkDeviceSize& outCapacity);
    void destroyPerFrame() noexcept;
    bool recreateSwapchain();
    void recordFrame(VkCommandBuffer cmd, std::uint32_t imageIndex,
                     const threadmaxx::RenderFrame& frame, PerFrame& pf);
    // §3.11.2 batch D2 — per-camera slice into the pre-packed
    // instance buffer. §3.11 batch 9b.2b — the single
    // (offset, count) pair is now a list of per-meshId sub-slices.
    // Default case (every visible instance has meshId == 0 or < 0)
    // collapses to one entry with `meshId == 0`, producing the
    // same single-bind+single-draw path as pre-9b.2b.
    struct MeshGroup {
        std::int32_t  meshId;
        VkDeviceSize  offsetBytes;
        std::uint32_t instanceCount;
        bool          skinned = false;   // §9b.4.b — dispatch selector
    };
    struct CameraSlice {
        std::vector<MeshGroup> groups;
    };
    void recordCamera(VkCommandBuffer cmd, const threadmaxx::Camera& cam,
                      std::uint32_t cameraIndex,
                      const threadmaxx::RenderFrame& frame,
                      PerFrame& pf,
                      const CameraSlice& slice);

    // §3.11 batch 9b.2b — resolve a meshId to a Mesh*. Returns the
    // default cube for `meshId == 0` or out-of-range / freed slots.
    const Mesh* lookupMesh(std::int32_t meshId) const noexcept;

    // §3.11.7b.5 batch 9b.4.b — resolve a skinnedMeshId. No default
    // fallback (cube isn't skinned-layout-compatible).
    const Mesh* lookupSkinnedMesh(std::int32_t meshId) const noexcept;
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
    if (const char* env = std::getenv("THREADMAXX_VK_PROFILE")) {
        impl_->profileEnabled = (env[0] != '\0' && env[0] != '0');
    }
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

    // ---- Asset loaders --------------------------------------------------
    //
    // §3.11 batch 9b.3 — loaders are now created BEFORE the pipelines
    // so `pipes.create` can register each shader stage with the loader
    // up front; that's what wires the hot-reload contract end-to-end.
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

    if (!impl_->pipes.create(impl_->ctx,
                             impl_->swapchain.colorFormat(),
                             impl_->swapchain.depthFormat(),
                             *impl_->shaderLoader,
                             *impl_->engine)) {
        return false;
    }

    impl_->perFrame.resize(impl_->cfg.framesInFlight);

    // Stand up the fallback unit-cube mesh that every Milestone-1
    // `RenderTag` entity batches against until batch 10 plugs in real
    // mesh assets.
    impl_->cubeHandle = impl_->meshLoader->createUnitCube(*impl_->engine);

    // §3.11.7b.5 batch 9b.4.b — stand up the bone descriptor pool +
    // per-frame descriptor sets now that the pipeline (and its
    // bone-set layout) exists. Descriptor sets are wired to each
    // frame's bone buffer lazily on first `setBoneMatrices` call.
    impl_->createBoneDescriptorResources();

    // §3.11 batch 9b.3 — subscribe to the engine's typed
    // `AssetReloaded` event channel. When the shader loader publishes
    // a reload for one of our pipeline shader ids, `rebuildIfMatches`
    // tears down + re-creates the affected pipeline. The
    // `Subscription` auto-detaches on Impl destruction.
    impl_->reloadSub = impl_->engine->events<threadmaxx::AssetReloaded>()
        .subscribeScoped([this](const threadmaxx::AssetReloaded& ev) {
            if (ev.type != std::type_index(typeid(Shader))) return;
            const threadmaxx::ResourceId<Shader> oldId{ev.oldIndex, ev.oldGeneration};
            const threadmaxx::ResourceId<Shader> newId{ev.newIndex, ev.newGeneration};
            const bool rebuilt = impl_->pipes.rebuildIfMatches(
                impl_->ctx, *impl_->engine, oldId, newId);
            if (rebuilt) {
                std::printf("[vulkan_renderer] pipeline rebuilt after shader reload "
                            "(old=%u/%u new=%u/%u)\n",
                            oldId.index, oldId.generation,
                            newId.index, newId.generation);
            }
        });

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

    // §3.11 batch 9b.2b — drop slot-table refs before the loader frees
    // GPU memory below. The loader's `releaseGpuResources` is the
    // authoritative free; this just decrements the registry refcounts
    // so the slots are clean for the engine-side teardown.
    impl_->meshSlots.clear();
    impl_->skinnedMeshSlots.clear();
    impl_->destroyBoneDescriptorResources();
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

void VulkanRenderer::setDefaultMesh(threadmaxx::ResourceHandle<Mesh> handle) noexcept {
    // §3.11 batch 9b.2 — replace the cached cube handle. Move-assign
    // so the previous handle's destructor runs and decrements the
    // refcount on the old slot.
    impl_->cubeHandle = std::move(handle);
}

bool VulkanRenderer::setDefaultMeshFromData(std::span<const float>         vertices,
                                            std::span<const std::uint16_t> indices) noexcept {
    if (!impl_->meshLoader || !impl_->engine) return false;
    auto handle = impl_->meshLoader->createMesh(*impl_->engine, vertices, indices);
    if (!handle.valid()) return false;
    impl_->cubeHandle = std::move(handle);
    return true;
}

std::int32_t VulkanRenderer::registerMesh(threadmaxx::ResourceHandle<Mesh> handle) {
    if (!handle.valid()) return -1;
    impl_->meshSlots.push_back(std::move(handle));
    // Slot indices 1..N; slot 0 is reserved for the default mesh.
    return static_cast<std::int32_t>(impl_->meshSlots.size());
}

std::int32_t VulkanRenderer::registerMeshFromData(std::span<const float>         vertices,
                                                  std::span<const std::uint16_t> indices) noexcept {
    if (!impl_->meshLoader || !impl_->engine) return -1;
    auto handle = impl_->meshLoader->createMesh(*impl_->engine, vertices, indices);
    if (!handle.valid()) return -1;
    return registerMesh(std::move(handle));
}

std::int32_t VulkanRenderer::registerSkinnedMeshFromData(
    std::span<const float>         vertices,
    std::span<const std::uint16_t> indices) noexcept {
    // §3.11.7b.5 batch 9b.4.b — same upload path as `createMesh`,
    // but the resulting handle goes into the skinned slot table.
    // We don't check the per-vertex stride here (the input is a flat
    // float span); callers must match the skinned pipeline's 56-byte
    // layout (14 floats per vertex). A regression in that contract
    // would surface as Vulkan validation errors or garbage geometry.
    if (!impl_->meshLoader || !impl_->engine) return -1;
    auto handle = impl_->meshLoader->createMesh(*impl_->engine, vertices, indices);
    if (!handle.valid()) return -1;
    impl_->skinnedMeshSlots.push_back(std::move(handle));
    return static_cast<std::int32_t>(impl_->skinnedMeshSlots.size());
}

void VulkanRenderer::setBoneMatrices(std::span<const float> matrices) noexcept {
    // §3.11.7b.5 batch 9b.4.b — write the bone matrix array into the
    // CURRENT back frame's bone buffer + update its descriptor set.
    // The frame index advances inside `submitFrame`, so this call
    // must happen BEFORE `submitFrame` for the matrices to land in
    // the right slot. Empty input is a valid no-op (no skinned
    // entities this tick).
    if (matrices.empty()) return;
    if (impl_->perFrame.empty()) return;

    const std::uint32_t fi = impl_->frameIndex;
    if (fi >= impl_->perFrame.size()) return;
    auto& pf = impl_->perFrame[fi];

    const VkDeviceSize bytes = matrices.size_bytes();
    const VkBuffer oldHandle = pf.boneBuffer;
    impl_->ensureBuffer(pf, bytes,
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                        pf.boneBuffer, pf.boneMemory, pf.boneCapacity);

    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(impl_->ctx.device(), pf.boneMemory, 0, bytes, 0, &mapped));
    std::memcpy(mapped, matrices.data(), bytes);
    vkUnmapMemory(impl_->ctx.device(), pf.boneMemory);
    pf.boneSize = bytes;

    // If `ensureBuffer` (re)created the buffer, the descriptor set's
    // bound handle is stale — rewrite it. The first call after init
    // also hits this path (oldHandle was VK_NULL_HANDLE).
    if (oldHandle != pf.boneBuffer) {
        impl_->updateBoneDescriptor(fi, pf);
    }
}

void VulkanRenderer::reloadShaders() {
    if (!impl_->engine) return;
    int requested = 0;
    for (std::size_t i = 0; i < static_cast<std::size_t>(PipelineShaderSlot::Count); ++i) {
        const auto slot = static_cast<PipelineShaderSlot>(i);
        const auto id = impl_->pipes.shaderId(slot);
        if (!id.valid()) continue;
        impl_->engine->markResourceStale<Shader>(id);
        ++requested;
    }
    std::printf("[vulkan_renderer] reloadShaders: %d stale request(s) queued\n",
                requested);
}

// §3.11.7b.5 batch 9b.4.b — resolve a skinned meshId to its
// `Mesh*`. Returns null if the slot is out of range or freed —
// unlike `lookupMesh` we don't fall back to the default cube
// because the cube isn't laid out as a 56-byte skinned vertex.
const Mesh* VulkanRenderer::Impl::lookupSkinnedMesh(std::int32_t meshId) const noexcept {
    if (meshId <= 0) return nullptr;
    const std::size_t slot = static_cast<std::size_t>(meshId - 1);
    if (slot >= skinnedMeshSlots.size()) return nullptr;
    const auto& h = skinnedMeshSlots[slot];
    if (!h.valid()) return nullptr;
    return h.get();
}

const Mesh* VulkanRenderer::Impl::lookupMesh(std::int32_t meshId) const noexcept {
    if (meshId <= 0) {
        return cubeHandle.valid() ? cubeHandle.get() : nullptr;
    }
    const std::size_t slot = static_cast<std::size_t>(meshId - 1);
    if (slot >= meshSlots.size()) {
        return cubeHandle.valid() ? cubeHandle.get() : nullptr;
    }
    const auto& h = meshSlots[slot];
    if (!h.valid()) {
        return cubeHandle.valid() ? cubeHandle.get() : nullptr;
    }
    return h.get();
}

void VulkanRenderer::submitFrame(const threadmaxx::RenderFrame& frame) {
    using clk = std::chrono::steady_clock;
    const bool prof = impl_->profileEnabled;
    auto tStart = prof ? clk::now() : clk::time_point{};

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
    auto tAfterWait = prof ? clk::now() : clk::time_point{};

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
    auto tAfterAcquire = prof ? clk::now() : clk::time_point{};

    VK_CHECK(vkResetCommandBuffer(slot.cmd, 0));
    VkCommandBufferBeginInfo bi = {};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(slot.cmd, &bi));

    impl_->recordFrame(slot.cmd, imageIndex, frame, impl_->perFrame[fi]);

    VK_CHECK(vkEndCommandBuffer(slot.cmd));
    auto tAfterRecord = prof ? clk::now() : clk::time_point{};

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
    auto tAfterSubmit = prof ? clk::now() : clk::time_point{};

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

    if (prof) {
        auto tEnd = clk::now();
        const auto ns = [](auto a, auto b) {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
        };
        impl_->profileSumWaitNs    += ns(tStart,         tAfterWait);
        impl_->profileSumAcquireNs += ns(tAfterWait,     tAfterAcquire);
        impl_->profileSumRecordNs  += ns(tAfterAcquire,  tAfterRecord);
        impl_->profileSumSubmitNs  += ns(tAfterRecord,   tAfterSubmit);
        impl_->profileSumPresentNs += ns(tAfterSubmit,   tEnd);
        impl_->profileFrameCount++;
        if (impl_->profileFrameCount >= Impl::kProfilePeriod) {
            const double n = static_cast<double>(impl_->profileFrameCount);
            std::fprintf(stderr,
                "[vk_prof] avg over %llu frames (ms): "
                "wait=%.2f acq=%.2f rec=%.2f (pack=%.2f) sub=%.2f pres=%.2f "
                "draws/f=%.1f insts/f=%.1f\n",
                static_cast<unsigned long long>(impl_->profileFrameCount),
                impl_->profileSumWaitNs    / n / 1.0e6,
                impl_->profileSumAcquireNs / n / 1.0e6,
                impl_->profileSumRecordNs  / n / 1.0e6,
                impl_->profileSumPackNs    / n / 1.0e6,
                impl_->profileSumSubmitNs  / n / 1.0e6,
                impl_->profileSumPresentNs / n / 1.0e6,
                impl_->profileSumDrawCalls / n,
                impl_->profileSumInstances / n);
            impl_->profileFrameCount = 0;
            impl_->profileSumWaitNs    = 0;
            impl_->profileSumAcquireNs = 0;
            impl_->profileSumRecordNs  = 0;
            impl_->profileSumSubmitNs  = 0;
            impl_->profileSumPresentNs = 0;
            impl_->profileSumPackNs    = 0;
            impl_->profileSumDrawCalls = 0;
            impl_->profileSumInstances = 0;
        }
    }
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
        if (pf.boneBuffer)       vkDestroyBuffer(ctx.device(), pf.boneBuffer,       nullptr);
        if (pf.boneMemory)       vkFreeMemory  (ctx.device(), pf.boneMemory,       nullptr);
        pf = {};
    }
    perFrame.clear();
}

void VulkanRenderer::Impl::createBoneDescriptorResources() {
    if (boneDescriptorPool != VK_NULL_HANDLE) return;
    const std::uint32_t framesIn = cfg.framesInFlight;

    VkDescriptorPoolSize poolSize = {};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = framesIn;

    VkDescriptorPoolCreateInfo pci = {};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = framesIn;
    pci.poolSizeCount = 1;
    pci.pPoolSizes = &poolSize;
    VK_CHECK(vkCreateDescriptorPool(ctx.device(), &pci, nullptr,
                                    &boneDescriptorPool));

    // Allocate one descriptor set per PerFrame slot, all sharing the
    // single bone-set layout. We allocate them all in one call (more
    // efficient than per-slot calls); descriptor sets get written
    // lazily on first `setBoneMatrices` call per slot.
    std::vector<VkDescriptorSetLayout> layouts(framesIn,
                                               pipes.opaqueSkinnedBoneSetLayout());
    std::vector<VkDescriptorSet> allocated(framesIn);
    VkDescriptorSetAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = boneDescriptorPool;
    ai.descriptorSetCount = framesIn;
    ai.pSetLayouts = layouts.data();
    VK_CHECK(vkAllocateDescriptorSets(ctx.device(), &ai, allocated.data()));

    if (perFrame.size() < framesIn) perFrame.resize(framesIn);
    for (std::uint32_t i = 0; i < framesIn; ++i) {
        perFrame[i].boneDescriptorSet = allocated[i];
    }
}

void VulkanRenderer::Impl::updateBoneDescriptor(std::uint32_t /*fi*/, PerFrame& pf) {
    if (pf.boneDescriptorSet == VK_NULL_HANDLE || pf.boneBuffer == VK_NULL_HANDLE) return;
    VkDescriptorBufferInfo bi = {};
    bi.buffer = pf.boneBuffer;
    bi.offset = 0;
    bi.range  = VK_WHOLE_SIZE;
    VkWriteDescriptorSet w = {};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = pf.boneDescriptorSet;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo = &bi;
    vkUpdateDescriptorSets(ctx.device(), 1, &w, 0, nullptr);
}

void VulkanRenderer::Impl::destroyBoneDescriptorResources() noexcept {
    if (boneDescriptorPool) {
        // Pool destruction implicitly frees the allocated sets.
        vkDestroyDescriptorPool(ctx.device(), boneDescriptorPool, nullptr);
        boneDescriptorPool = VK_NULL_HANDLE;
    }
    for (auto& pf : perFrame) pf.boneDescriptorSet = VK_NULL_HANDLE;
}

bool VulkanRenderer::Impl::recreateSwapchain() {
    vkDeviceWaitIdle(ctx.device());
    swapchain.destroy(ctx);

    const std::uint32_t w = pendingWidth  ? pendingWidth  : cfg.width;
    const std::uint32_t h = pendingHeight ? pendingHeight : cfg.height;
    if (!swapchain.create(ctx, window, w, h)) {
        return false;
    }
    // §3.11 batch 9b.3 — recreate ONLY the pipelines; don't re-register
    // shaders with the loader (that would duplicate entries).
    if (!pipes.recreatePipelines(ctx,
                                 swapchain.colorFormat(),
                                 swapchain.depthFormat(),
                                 *engine)) {
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
    auto tPackStart = profileEnabled ? std::chrono::steady_clock::now()
                                     : std::chrono::steady_clock::time_point{};
    std::uint64_t totalInstancesThisFrame = 0;
    if (frame.cameras.empty()) {
        // No cameras pushed by user systems → nothing to project. The
        // clear color above is the only visible output. This is the
        // smoke / first-tick state.
    } else {
        // §3.11.2 batch D2 — pre-pack every camera's instance slice
        // into one contiguous buffer BEFORE the camera loop starts,
        // so per-camera `ensureBuffer` calls can't destroy a buffer
        // that an earlier camera already bound.
        //
        // §3.11 batch 9b.2b — within each camera's slice, instances are
        // bucketed by meshId so the per-camera draw loop can bind the
        // matching mesh and issue one `vkCmdDrawIndexed` per mesh.
        // Bucket iteration order is insertion order (the meshIds the
        // camera first sees), giving deterministic draw ordering. With
        // a single meshId the bucket list collapses to one entry,
        // matching pre-9b.2b output bit-for-bit.
        std::vector<threadmaxx::InstanceLayoutEntry> packed;
        std::vector<CameraSlice> slices(frame.cameras.size());
        const std::size_t cameraCount =
            std::min<std::size_t>(frame.cameras.size(), 32);

        // §3.11.7b.5 batch 9b.4.b — buckets are now keyed by
        // (meshId, skinned). A skinned DrawItem with the same meshId
        // as an unskinned one would still want a separate bucket
        // because they hit different pipelines + meshes.
        auto appendToBucket = [&](std::int32_t meshId, bool skinned,
                                  const threadmaxx::InstanceLayoutEntry& e) {
            // Skinned items use the skinned slot namespace; meshId 0
            // is meaningless there (no default skinned mesh), so
            // skip without bucketing.
            if (skinned && meshId <= 0) return;
            const std::int32_t key = (!skinned && meshId < 0) ? 0 : meshId;
            for (std::size_t b = 0; b < pf.scratchMeshIds.size(); ++b) {
                if (pf.scratchMeshIds[b] == key &&
                    pf.scratchSkinned[b]  == skinned) {
                    pf.scratchBuckets[b].push_back(e);
                    return;
                }
            }
            pf.scratchMeshIds.push_back(key);
            pf.scratchSkinned.push_back(skinned);
            if (pf.scratchBuckets.size() < pf.scratchMeshIds.size()) {
                pf.scratchBuckets.emplace_back();
            } else {
                pf.scratchBuckets[pf.scratchMeshIds.size() - 1].clear();
            }
            pf.scratchBuckets[pf.scratchMeshIds.size() - 1].push_back(e);
        };

        for (std::size_t i = 0; i < cameraCount; ++i) {
            const std::uint32_t bit = (1u << i);
            // Reset scratch state for this camera. Capacities persist.
            pf.scratchMeshIds.clear();
            pf.scratchSkinned.clear();
            for (auto& bucket : pf.scratchBuckets) bucket.clear();

            // Auto-instance lane (Milestone-1 contract): always visible.
            // Auto-instances are never skinned (they're the legacy
            // RenderTag-only entities).
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
                appendToBucket(virt.meshId, /*skinned=*/false,
                               threadmaxx::packInstance(virt));
            }
            // Camera-mask-filtered opaque items. `skeletonId >= 0`
            // is the public signal that the item should use the
            // skinned pipeline (§3.11.7b.5 batch 9b.4.b).
            const auto opaqueItems = frame.drawItems[threadmaxx::passIndex(
                threadmaxx::RenderPass::Opaque)];
            for (const auto& di : opaqueItems) {
                if ((di.cameraMask & bit) == 0) continue;
                appendToBucket(di.meshId, di.skeletonId >= 0,
                               threadmaxx::packInstance(di));
            }

            // Flush buckets to `packed[]` in insertion order and record
            // the per-bucket sub-slice. Empty buckets are filtered.
            slices[i].groups.clear();
            for (std::size_t b = 0; b < pf.scratchMeshIds.size(); ++b) {
                const auto& bucket = pf.scratchBuckets[b];
                if (bucket.empty()) continue;
                MeshGroup g{};
                g.meshId        = pf.scratchMeshIds[b];
                g.skinned       = pf.scratchSkinned[b];
                g.offsetBytes   = sizeof(threadmaxx::InstanceLayoutEntry) *
                                  packed.size();
                g.instanceCount = static_cast<std::uint32_t>(bucket.size());
                packed.insert(packed.end(), bucket.begin(), bucket.end());
                slices[i].groups.push_back(g);
            }
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
        totalInstancesThisFrame = static_cast<std::uint64_t>(packed.size());

        std::uint64_t drawsThisFrame = 0;
        // 2026-05-20 — build the debug line / point vertex buffers ONCE
        // for the whole frame (cameras share the buffer; contents don't
        // depend on the camera). Previously recordCamera rebuilt these
        // per camera and used vector::insert(end(), {init_list}) per
        // endpoint, which dominated submitFrame at high line counts.
        pf.debugLineCount  = 0;
        pf.debugPointCount = 0;
        if (!frame.debugLines.empty()) {
            const std::size_t nLines = frame.debugLines.size();
            pf.debugLineVerts.resize(nLines * 14u);  // 2 endpoints × (3 pos + 4 col)
            float* dst = pf.debugLineVerts.data();
            for (std::size_t li = 0; li < nLines; ++li) {
                const auto& l = frame.debugLines[li];
                float col[4];
                unpackRGBA(l.colorRGBA, col);
                dst[ 0] = l.a.x; dst[ 1] = l.a.y; dst[ 2] = l.a.z;
                dst[ 3] = col[0]; dst[ 4] = col[1]; dst[ 5] = col[2]; dst[ 6] = col[3];
                dst[ 7] = l.b.x; dst[ 8] = l.b.y; dst[ 9] = l.b.z;
                dst[10] = col[0]; dst[11] = col[1]; dst[12] = col[2]; dst[13] = col[3];
                dst += 14;
            }
            const VkDeviceSize bytes =
                static_cast<VkDeviceSize>(pf.debugLineVerts.size()) * sizeof(float);
            ensureBuffer(pf, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         pf.debugLineBuffer, pf.debugLineMemory,
                         pf.debugLineCapacity);
            void* p = nullptr;
            VK_CHECK(vkMapMemory(ctx.device(), pf.debugLineMemory, 0, bytes, 0, &p));
            std::memcpy(p, pf.debugLineVerts.data(), bytes);
            vkUnmapMemory(ctx.device(), pf.debugLineMemory);
            pf.debugLineCount = static_cast<std::uint32_t>(nLines);
        }
        if (!frame.debugPoints.empty()) {
            const std::size_t nPts = frame.debugPoints.size();
            pf.debugPointVerts.resize(nPts * 8u);
            float* dst = pf.debugPointVerts.data();
            for (std::size_t pi = 0; pi < nPts; ++pi) {
                const auto& pt = frame.debugPoints[pi];
                float col[4];
                unpackRGBA(pt.colorRGBA, col);
                dst[0] = pt.position.x; dst[1] = pt.position.y; dst[2] = pt.position.z;
                dst[3] = col[0]; dst[4] = col[1]; dst[5] = col[2]; dst[6] = col[3];
                dst[7] = pt.pixelSize;
                dst += 8;
            }
            const VkDeviceSize bytes =
                static_cast<VkDeviceSize>(pf.debugPointVerts.size()) * sizeof(float);
            ensureBuffer(pf, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                         pf.debugPointBuffer, pf.debugPointMemory,
                         pf.debugPointCapacity);
            void* p = nullptr;
            VK_CHECK(vkMapMemory(ctx.device(), pf.debugPointMemory, 0, bytes, 0, &p));
            std::memcpy(p, pf.debugPointVerts.data(), bytes);
            vkUnmapMemory(ctx.device(), pf.debugPointMemory);
            pf.debugPointCount = static_cast<std::uint32_t>(nPts);
        }

        if (profileEnabled) {
            auto tPackEnd = std::chrono::steady_clock::now();
            profileSumPackNs += static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    tPackEnd - tPackStart).count());
        }

        for (std::size_t i = 0; i < cameraCount; ++i) {
            recordCamera(cmd, frame.cameras[i],
                         static_cast<std::uint32_t>(i),
                         frame, pf, slices[i]);
            for (const auto& g : slices[i].groups) {
                if (g.instanceCount > 0) ++drawsThisFrame;
            }
        }
        if (profileEnabled) {
            profileSumDrawCalls += drawsThisFrame;
            profileSumInstances += totalInstancesThisFrame;
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
    //
    // 2026-05-20 — for cameras whose viewport doesn't cover the full
    // framebuffer (mini-map, aim PIP), clear color + depth inside the
    // scissor rect BEFORE drawing. Without this each non-first camera
    // inherits the previous camera's depth values inside its viewport,
    // which makes opaque draws fail or pass tests against unrelated
    // depths (visually: terrain bleeds over entities on the mini-map;
    // the PIP shows a chaotic mix of main-camera pixels and PIP
    // pixels). The full-screen main camera (cameraIndex==0 with
    // viewport == [0,0,1,1]) skips the clear since the renderpass-
    // level loadOp already cleared.
    const VkExtent2D ext = swapchain.extent();
    const float fw = static_cast<float>(ext.width);
    const float fh = static_cast<float>(ext.height);
    const float vx = cam.viewport.x      * fw;
    const float vy = cam.viewport.y      * fh;
    const float vw = cam.viewport.width  * fw;
    const float vh = cam.viewport.height * fh;
    {
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
    const bool isFullScreen =
        cam.viewport.x      == 0.0f &&
        cam.viewport.y      == 0.0f &&
        cam.viewport.width  == 1.0f &&
        cam.viewport.height == 1.0f;
    if (cameraIndex != 0 && !isFullScreen) {
        VkClearAttachment clears[2] = {};
        clears[0].aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        clears[0].colorAttachment = 0;
        clears[0].clearValue.color = {{0.03f, 0.04f, 0.07f, 1.0f}};
        clears[1].aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        clears[1].clearValue.depthStencil = {1.0f, 0};
        VkClearRect rect = {};
        rect.rect.offset.x = static_cast<std::int32_t>(vx);
        rect.rect.offset.y = static_cast<std::int32_t>(vy);
        rect.rect.extent.width  = static_cast<std::uint32_t>(vw);
        rect.rect.extent.height = static_cast<std::uint32_t>(vh);
        rect.baseArrayLayer = 0;
        rect.layerCount     = 1;
        vkCmdClearAttachments(cmd, 2, clears, 1, &rect);
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
    // §3.11 batch 9b.2b — each camera's slice is now a list of
    // per-meshId sub-slices.
    // §3.11.7b.5 batch 9b.4.b — each group is also tagged with
    // `skinned`. We switch pipelines on demand (track the currently-
    // bound one to avoid redundant rebinds when groups happen to be
    // ordered with all-unskinned-then-all-skinned). Push constants
    // get re-pushed each pipeline switch because the pipeline
    // layouts differ between opaque and opaque-skinned.
    if (!slice.groups.empty() && cubeHandle.valid()) {
        OpaquePushConstants pc = {};
        std::memcpy(pc.viewProj, vp, sizeof(vp));
        pc.lightDir[0] = -0.3f;
        pc.lightDir[1] = -1.0f;
        pc.lightDir[2] = -0.2f;
        pc.cameraPos[0] = cam.position.x;
        pc.cameraPos[1] = cam.position.y;
        pc.cameraPos[2] = cam.position.z;

        enum class Bound : std::uint8_t { None, Opaque, OpaqueSkinned };
        Bound bound = Bound::None;

        for (const auto& g : slice.groups) {
            if (g.instanceCount == 0) continue;

            const Mesh* mesh = g.skinned
                ? lookupSkinnedMesh(g.meshId)
                : lookupMesh(g.meshId);
            if (!mesh || !mesh->gpuReady) continue;
            if (g.skinned && pf.boneDescriptorSet == VK_NULL_HANDLE) continue;

            // Bind / switch pipeline + push constants if needed.
            const Bound need = g.skinned ? Bound::OpaqueSkinned : Bound::Opaque;
            if (need != bound) {
                const VkPipeline       pipe   = g.skinned ? pipes.opaqueSkinnedPipe()   : pipes.opaquePipe();
                const VkPipelineLayout layout = g.skinned ? pipes.opaqueSkinnedLayout() : pipes.opaqueLayout();
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
                vkCmdPushConstants(cmd, layout,
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(pc), &pc);
                if (g.skinned) {
                    // Bind the bone descriptor set (set 0). The
                    // unskinned opaque pipeline has no descriptor
                    // sets, so we only bind here.
                    vkCmdBindDescriptorSets(cmd,
                                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            layout,
                                            /*firstSet=*/0,
                                            1, &pf.boneDescriptorSet,
                                            0, nullptr);
                }
                bound = need;
            }

            VkBuffer vbufs[2] = {mesh->vertexBuffer, pf.instanceBuffer};
            VkDeviceSize voffs[2] = {0, g.offsetBytes};
            vkCmdBindVertexBuffers(cmd, 0, 2, vbufs, voffs);
            vkCmdBindIndexBuffer(cmd, mesh->indexBuffer, 0, VK_INDEX_TYPE_UINT16);

            vkCmdDrawIndexed(cmd, mesh->indexCount,
                             g.instanceCount,
                             0, 0, 0);
        }
    }

    // ---- Debug lines / points (single shared pass) --------------------------
    // 2026-05-20 — CPU vertex packing + upload happens once in
    // recordFrame; here we just bind + draw against the shared buffer.
    if (pf.debugLineCount > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes.debugLinePipe());
        DebugPushConstants pc = {};
        std::memcpy(pc.viewProj, vp, sizeof(vp));
        vkCmdPushConstants(cmd, pipes.debugLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &pf.debugLineBuffer, &off);
        vkCmdDraw(cmd, pf.debugLineCount * 2u, 1, 0, 0);
    }

    if (pf.debugPointCount > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes.debugPointPipe());
        DebugPushConstants pc = {};
        std::memcpy(pc.viewProj, vp, sizeof(vp));
        vkCmdPushConstants(cmd, pipes.debugLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &pf.debugPointBuffer, &off);
        vkCmdDraw(cmd, pf.debugPointCount, 1, 0, 0);
    }
}

} // namespace threadmaxx_vk
