#pragma once

#include "VulkanContext.hpp"

#include <threadmaxx/Resource.hpp>
#include <threadmaxx_vk/Mesh.hpp>   // Shader POD lives here

#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <span>

namespace threadmaxx { class Engine; }

namespace threadmaxx_vk {

class ShaderLoader;

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

/// Identity tags for the pipeline shaders. Used by
/// @ref rebuildIfMatches to dispatch a hot-reload event to the
/// correct pipeline. §3.11.7b.5 batch 9b.4.a added
/// `OpaqueSkinnedVert`; the skinned pipeline reuses the existing
/// `OpaqueFrag` for its fragment stage (same `vNormal` + `vColor`
/// inputs from the vertex shader).
enum class PipelineShaderSlot : std::uint8_t {
    OpaqueVert = 0,
    OpaqueFrag = 1,
    DebugLineVert = 2,
    DebugLineFrag = 3,
    DebugPointVert = 4,
    DebugPointFrag = 5,
    OpaqueSkinnedVert = 6,
    Count = 7,
};

/// Builds and owns the three pipelines + their layouts. Pipelines depend
/// on the swapchain color/depth formats (via dynamic-rendering's pipeline
/// rendering info), so they're rebuilt whenever the swapchain is.
///
/// §3.11 batch 9b.3 — each shader stage is also registered with the
/// engine's @ref ShaderLoader so a `markResourceStale<Shader>` cycle
/// triggers `rebuildIfMatches` and tears down + re-creates the
/// affected pipeline using the new SPIR-V from disk.
class VulkanPipelines {
public:
    VulkanPipelines() = default;
    ~VulkanPipelines();

    VulkanPipelines(const VulkanPipelines&) = delete;
    VulkanPipelines& operator=(const VulkanPipelines&) = delete;

    /// Build all three pipelines. Registers every shader stage with the
    /// loader so the renderer can react to `AssetReloaded` events. The
    /// engine reference is borrowed for the registration call; the
    /// loader pointer is borrowed for its entire lifetime (the engine
    /// owns the loader).
    bool create(VulkanContext& ctx,
                VkFormat colorFormat,
                VkFormat depthFormat,
                ShaderLoader& shaderLoader,
                threadmaxx::Engine& engine);

    void destroy(VulkanContext& ctx) noexcept;

    /// §3.11 batch 9b.3 — rebuild all three pipelines using the
    /// currently-tracked shader handles. Called on swapchain
    /// recreate so we don't duplicate-register the shaders with the
    /// loader. Updates the cached `colorFormat_` / `depthFormat_` to
    /// the new swapchain formats. Pipeline layouts are NOT recreated.
    bool recreatePipelines(VulkanContext& ctx,
                           VkFormat colorFormat,
                           VkFormat depthFormat,
                           threadmaxx::Engine& engine);

    VkPipelineLayout opaqueLayout() const noexcept { return opaqueLayout_; }
    VkPipeline       opaquePipe()   const noexcept { return opaquePipe_; }

    VkPipelineLayout debugLayout()    const noexcept { return debugLayout_; }
    VkPipeline       debugLinePipe()  const noexcept { return debugLinePipe_; }
    VkPipeline       debugPointPipe() const noexcept { return debugPointPipe_; }

    /// §3.11.7b.5 batch 9b.4.a — skinned-mesh pipeline. Currently
    /// created at renderer init but never bound to a draw; 9b.4.b
    /// will plumb the per-frame pose buffer + descriptor set binding,
    /// 9b.4.c will land glTF skinned-mesh import + demo wiring.
    VkPipelineLayout opaqueSkinnedLayout() const noexcept { return opaqueSkinnedLayout_; }
    VkPipeline       opaqueSkinnedPipe()   const noexcept { return opaqueSkinnedPipe_; }
    /// Descriptor set layout for the bone matrix SSBO at set 0,
    /// binding 0. Exposed so 9b.4.b can allocate matching descriptor
    /// sets from a pool.
    VkDescriptorSetLayout opaqueSkinnedBoneSetLayout() const noexcept {
        return opaqueSkinnedBoneSetLayout_;
    }

    /// §3.11 batch 9b.3 — react to a shader hot-reload. Walks the
    /// tracked shader slots; if `oldId` matches one, fetches the new
    /// SPIR-V from `engine.resources().get<Shader>(newId)`, calls
    /// `vkDeviceWaitIdle`, destroys the affected pipeline, and
    /// rebuilds it. Returns true if a rebuild occurred. The pipeline
    /// layouts are NOT recreated — only the `VkPipeline` that owned
    /// the changed shader gets torn down.
    bool rebuildIfMatches(VulkanContext& ctx,
                          threadmaxx::Engine& engine,
                          threadmaxx::ResourceId<Shader> oldId,
                          threadmaxx::ResourceId<Shader> newId);

    /// Each pipeline's currently-tracked shader handles. Returns
    /// `valid() == false` for any slot that wasn't registered.
    threadmaxx::ResourceId<Shader> shaderId(PipelineShaderSlot slot) const noexcept;

private:
    static VkShaderModule makeShader(VkDevice device, std::span<const std::uint32_t> spirv);

    // Per-pipeline creation helpers. Take shader modules + cached
    // format state; produce a fresh VkPipeline.
    VkPipeline buildOpaquePipeline(VkDevice device,
                                   VkShaderModule vs, VkShaderModule fs,
                                   VkFormat colorFormat, VkFormat depthFormat);
    VkPipeline buildDebugLinePipeline(VkDevice device,
                                      VkShaderModule vs, VkShaderModule fs,
                                      VkFormat colorFormat, VkFormat depthFormat);
    VkPipeline buildDebugPointPipeline(VkDevice device,
                                       VkShaderModule vs, VkShaderModule fs,
                                       VkFormat colorFormat, VkFormat depthFormat);
    /// §3.11.7b.5 batch 9b.4.a — skinned pipeline build helper.
    /// Vertex layout has the same per-instance binding as the
    /// non-skinned opaque path PLUS per-vertex bone IDs + weights
    /// (locations 8 + 9). The pipeline layout chains the existing
    /// push-constant range with a single descriptor-set layout for
    /// the bone matrix SSBO at set 0, binding 0.
    VkPipeline buildOpaqueSkinnedPipeline(VkDevice device,
                                          VkShaderModule vs, VkShaderModule fs,
                                          VkFormat colorFormat, VkFormat depthFormat);

    // Rebuild helpers for the hot-reload path. Each rebuilds the
    // affected pipeline by fetching the CURRENT shader bytes for both
    // stages from the registry (the changed stage uses newSpirv; the
    // unchanged stage uses its tracked id's existing bytes).
    bool rebuildOpaque(VulkanContext& ctx, threadmaxx::Engine& engine);
    bool rebuildDebugLine(VulkanContext& ctx, threadmaxx::Engine& engine);
    bool rebuildDebugPoint(VulkanContext& ctx, threadmaxx::Engine& engine);
    /// §3.11.7b.5 batch 9b.4.a — rebuild the skinned pipeline using
    /// the current `OpaqueSkinnedVert` + `OpaqueFrag` SPIR-V from the
    /// registry. Same vkDeviceWaitIdle-then-destroy-then-recreate
    /// pattern as the other rebuilds.
    bool rebuildOpaqueSkinned(VulkanContext& ctx, threadmaxx::Engine& engine);

    VkPipelineLayout opaqueLayout_     = VK_NULL_HANDLE;
    VkPipeline       opaquePipe_       = VK_NULL_HANDLE;

    VkPipelineLayout debugLayout_      = VK_NULL_HANDLE;
    VkPipeline       debugLinePipe_    = VK_NULL_HANDLE;
    VkPipeline       debugPointPipe_   = VK_NULL_HANDLE;

    // §3.11.7b.5 batch 9b.4.a — skinned pipeline state.
    VkDescriptorSetLayout opaqueSkinnedBoneSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout      opaqueSkinnedLayout_        = VK_NULL_HANDLE;
    VkPipeline            opaqueSkinnedPipe_          = VK_NULL_HANDLE;

    // §3.11 batch 9b.3 — per-stage shader handles. The loader owns
    // the slots; we hold copies so refcounts pin them open.
    std::array<threadmaxx::ResourceHandle<Shader>,
               static_cast<std::size_t>(PipelineShaderSlot::Count)> shaders_;

    // Cached at create() time so rebuilds can reproduce the original
    // pipeline state without re-querying the swapchain.
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
};

} // namespace threadmaxx_vk
