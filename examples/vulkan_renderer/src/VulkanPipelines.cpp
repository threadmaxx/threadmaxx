#include "VulkanPipelines.hpp"

#include "ShaderLoader.hpp"
#include "VkUtil.hpp"

#include <threadmaxx/Engine.hpp>

#include "opaque_vert_spv.hpp"
#include "opaque_frag_spv.hpp"
#include "opaque_skinned_vert_spv.hpp"
#include "debug_line_vert_spv.hpp"
#include "debug_line_frag_spv.hpp"
#include "debug_point_vert_spv.hpp"
#include "debug_point_frag_spv.hpp"
#include "background_vert_spv.hpp"
#include "background_frag_spv.hpp"

#include <array>
#include <cstdio>
#include <filesystem>

namespace threadmaxx_vk {

VulkanPipelines::~VulkanPipelines() = default;

VkShaderModule VulkanPipelines::makeShader(VkDevice device,
                                           std::span<const std::uint32_t> spirv) {
    VkShaderModuleCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spirv.size_bytes();
    ci.pCode = spirv.data();
    VkShaderModule m = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device, &ci, nullptr, &m));
    return m;
}

namespace {

#ifndef THREADMAXX_VK_SHADER_DIR
#error "THREADMAXX_VK_SHADER_DIR must be defined by the build system"
#endif

// §3.11 batch 9b.3 — resolve the on-disk .spv path for a given
// pipeline stage. The CMake build copies / generates these files into
// `THREADMAXX_VK_SHADER_DIR` (= `${CMAKE_BINARY_DIR}/examples/vulkan_renderer/generated`).
// When `ShaderLoader::markStale` fires for one of these ids, the
// loader's `update()` re-reads the file and emits `AssetReloaded`.
std::filesystem::path shaderPathFor(PipelineShaderSlot slot) {
    const char* file = nullptr;
    switch (slot) {
        case PipelineShaderSlot::OpaqueVert:        file = "opaque.vert.spv";         break;
        case PipelineShaderSlot::OpaqueFrag:        file = "opaque.frag.spv";         break;
        case PipelineShaderSlot::OpaqueSkinnedVert: file = "opaque_skinned.vert.spv"; break;
        case PipelineShaderSlot::DebugLineVert:     file = "debug_line.vert.spv";     break;
        case PipelineShaderSlot::DebugLineFrag:     file = "debug_line.frag.spv";     break;
        case PipelineShaderSlot::DebugPointVert:    file = "debug_point.vert.spv";    break;
        case PipelineShaderSlot::DebugPointFrag:    file = "debug_point.frag.spv";    break;
        case PipelineShaderSlot::BackgroundVert:    file = "background.vert.spv";     break;
        case PipelineShaderSlot::BackgroundFrag:    file = "background.frag.spv";     break;
        case PipelineShaderSlot::Count:             file = "<invalid>";               break;
    }
    return std::filesystem::path(THREADMAXX_VK_SHADER_DIR) / file;
}

// Snapshot of the standard rasterizer / multisample / blend / viewport
// state shared across pipelines. Inlined here so each pipeline-build
// helper can call it without crossing translation-unit boundaries.
void fillStandardState(VkPipelineRasterizationStateCreateInfo& rs,
                       VkPipelineMultisampleStateCreateInfo& ms,
                       VkPipelineColorBlendAttachmentState& cb,
                       VkPipelineColorBlendStateCreateInfo& cbs,
                       VkPipelineViewportStateCreateInfo& vp) {
    rs = {};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    ms = {};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    cb = {};
    cb.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    cbs = {};
    cbs.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cbs.attachmentCount = 1;
    cbs.pAttachments = &cb;

    vp = {};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount  = 1;
}

// Embedded SPIR-V tables — one entry per shader slot. Used as the
// initial bytes registered with the loader; the on-disk version is
// what subsequent re-reads pull in.
std::span<const std::uint32_t> embeddedSpvFor(PipelineShaderSlot slot) {
    switch (slot) {
        case PipelineShaderSlot::OpaqueVert:        return k_opaque_vert_spv;
        case PipelineShaderSlot::OpaqueFrag:        return k_opaque_frag_spv;
        case PipelineShaderSlot::OpaqueSkinnedVert: return k_opaque_skinned_vert_spv;
        case PipelineShaderSlot::DebugLineVert:     return k_debug_line_vert_spv;
        case PipelineShaderSlot::DebugLineFrag:     return k_debug_line_frag_spv;
        case PipelineShaderSlot::DebugPointVert:    return k_debug_point_vert_spv;
        case PipelineShaderSlot::DebugPointFrag:    return k_debug_point_frag_spv;
        case PipelineShaderSlot::BackgroundVert:    return k_background_vert_spv;
        case PipelineShaderSlot::BackgroundFrag:    return k_background_frag_spv;
        case PipelineShaderSlot::Count: break;
    }
    return {};
}

// Fetch the current SPIR-V bytes for a shader id from the engine's
// resource registry. Returns an empty span if the id is stale.
std::span<const std::uint32_t> spvFromRegistry(threadmaxx::Engine& engine,
                                               threadmaxx::ResourceId<Shader> id) {
    if (!id.valid()) return {};
    const Shader* s = engine.resources().get<Shader>(id);
    if (!s) return {};
    return std::span<const std::uint32_t>(s->spirv.data(), s->spirv.size());
}

} // namespace

VkPipeline VulkanPipelines::buildOpaquePipeline(VkDevice device,
                                                VkShaderModule vs,
                                                VkShaderModule fs,
                                                VkFormat colorFormat,
                                                VkFormat depthFormat) {
    std::array<VkPipelineShaderStageCreateInfo, 2> stages = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bindings[2] = {};
    bindings[0].binding = 0;
    bindings[0].stride = sizeof(float) * 6;
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding = 1;
    bindings[1].stride = 128;
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attrs[6] = {};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0};                  // pos
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT,    sizeof(float) * 3};  // normal
    attrs[2] = {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0};                  // instPos
    attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16};                 // instOrientation
    attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32};                 // instScale
    attrs[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48};                 // instMatOverride

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 2;
    vi.pVertexBindingDescriptions = bindings;
    vi.vertexAttributeDescriptionCount = 6;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineRasterizationStateCreateInfo rs;
    VkPipelineMultisampleStateCreateInfo   ms;
    VkPipelineColorBlendAttachmentState    cb;
    VkPipelineColorBlendStateCreateInfo    cbs;
    VkPipelineViewportStateCreateInfo      vp;
    fillStandardState(rs, ms, cb, cbs, vp);

    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn = {};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkPipelineRenderingCreateInfo rci = {};
    rci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rci.colorAttachmentCount = 1;
    rci.pColorAttachmentFormats = &colorFormat;
    rci.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo gc = {};
    gc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gc.pNext = &rci;
    gc.stageCount = static_cast<std::uint32_t>(stages.size());
    gc.pStages = stages.data();
    gc.pVertexInputState = &vi;
    gc.pInputAssemblyState = &ia;
    gc.pViewportState = &vp;
    gc.pRasterizationState = &rs;
    gc.pMultisampleState = &ms;
    gc.pDepthStencilState = &ds;
    gc.pColorBlendState = &cbs;
    gc.pDynamicState = &dyn;
    gc.layout = opaqueLayout_;

    VkPipeline pipe = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gc, nullptr, &pipe));
    return pipe;
}

VkPipeline VulkanPipelines::buildDebugLinePipeline(VkDevice device,
                                                   VkShaderModule vs,
                                                   VkShaderModule fs,
                                                   VkFormat colorFormat,
                                                   VkFormat depthFormat) {
    std::array<VkPipelineShaderStageCreateInfo, 2> stages = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs; stages[1].pName = "main";

    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = sizeof(float) * 7;    // pos(3) + color(4)
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2] = {};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 3};

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkPipelineRasterizationStateCreateInfo rs;
    VkPipelineMultisampleStateCreateInfo   ms;
    VkPipelineColorBlendAttachmentState    cb;
    VkPipelineColorBlendStateCreateInfo    cbs;
    VkPipelineViewportStateCreateInfo      vp;
    fillStandardState(rs, ms, cb, cbs, vp);
    rs.cullMode = VK_CULL_MODE_NONE;

    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn = {};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkPipelineRenderingCreateInfo rci = {};
    rci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rci.colorAttachmentCount = 1;
    rci.pColorAttachmentFormats = &colorFormat;
    rci.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo gc = {};
    gc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gc.pNext = &rci;
    gc.stageCount = static_cast<std::uint32_t>(stages.size());
    gc.pStages = stages.data();
    gc.pVertexInputState = &vi;
    gc.pInputAssemblyState = &ia;
    gc.pViewportState = &vp;
    gc.pRasterizationState = &rs;
    gc.pMultisampleState = &ms;
    gc.pDepthStencilState = &ds;
    gc.pColorBlendState = &cbs;
    gc.pDynamicState = &dyn;
    gc.layout = debugLayout_;

    VkPipeline pipe = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gc, nullptr, &pipe));
    return pipe;
}

VkPipeline VulkanPipelines::buildDebugPointPipeline(VkDevice device,
                                                    VkShaderModule vs,
                                                    VkShaderModule fs,
                                                    VkFormat colorFormat,
                                                    VkFormat depthFormat) {
    std::array<VkPipelineShaderStageCreateInfo, 2> stages = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs; stages[1].pName = "main";

    VkVertexInputBindingDescription binding = {};
    binding.binding = 0;
    binding.stride = sizeof(float) * 8;    // pos(3) + color(4) + size(1)
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[3] = {};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 3};
    attrs[2] = {2, 0, VK_FORMAT_R32_SFLOAT,          sizeof(float) * 7};

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 3;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

    VkPipelineRasterizationStateCreateInfo rs;
    VkPipelineMultisampleStateCreateInfo   ms;
    VkPipelineColorBlendAttachmentState    cb;
    VkPipelineColorBlendStateCreateInfo    cbs;
    VkPipelineViewportStateCreateInfo      vp;
    fillStandardState(rs, ms, cb, cbs, vp);
    rs.cullMode = VK_CULL_MODE_NONE;

    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn = {};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkPipelineRenderingCreateInfo rci = {};
    rci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rci.colorAttachmentCount = 1;
    rci.pColorAttachmentFormats = &colorFormat;
    rci.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo gc = {};
    gc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gc.pNext = &rci;
    gc.stageCount = static_cast<std::uint32_t>(stages.size());
    gc.pStages = stages.data();
    gc.pVertexInputState = &vi;
    gc.pInputAssemblyState = &ia;
    gc.pViewportState = &vp;
    gc.pRasterizationState = &rs;
    gc.pMultisampleState = &ms;
    gc.pDepthStencilState = &ds;
    gc.pColorBlendState = &cbs;
    gc.pDynamicState = &dyn;
    gc.layout = debugLayout_;

    VkPipeline pipe = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gc, nullptr, &pipe));
    return pipe;
}

VkPipeline VulkanPipelines::buildOpaqueSkinnedPipeline(VkDevice device,
                                                       VkShaderModule vs,
                                                       VkShaderModule fs,
                                                       VkFormat colorFormat,
                                                       VkFormat depthFormat) {
    std::array<VkPipelineShaderStageCreateInfo, 2> stages = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";

    // §3.11.7b.5 batch 9b.4.a — skinned vertex format:
    // pos[3]f + normal[3]f + boneIDs[4]u32 + boneWeights[4]f = 56 bytes.
    VkVertexInputBindingDescription bindings[2] = {};
    bindings[0].binding = 0;
    bindings[0].stride = 56;   // 12 + 12 + 16 + 16
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding = 1;
    bindings[1].stride = 128;
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

    VkVertexInputAttributeDescription attrs[8] = {};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,    0};                  // pos
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT,    sizeof(float) * 3};  // normal
    attrs[2] = {2, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 0};                  // instPos
    attrs[3] = {3, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 16};                 // instOrientation
    attrs[4] = {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 32};                 // instScale
    attrs[5] = {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, 48};                 // instMatOverride
    attrs[6] = {6, 1, VK_FORMAT_R32G32B32A32_SINT,   64};                 // instMeshMat (needs .w for boneBase)
    // location 8: boneIDs (uvec4 → R32G32B32A32_UINT)
    attrs[7] = {8, 0, VK_FORMAT_R32G32B32A32_UINT,   sizeof(float) * 6};

    // NOTE: boneWeights (location 9) shares the same binding as
    // boneIDs but at offset 40. We need a 9th attr slot — extend the
    // array.

    VkVertexInputAttributeDescription attrs2[9] = {};
    for (int i = 0; i < 8; ++i) attrs2[i] = attrs[i];
    attrs2[8] = {9, 0, VK_FORMAT_R32G32B32A32_SFLOAT, sizeof(float) * 6 + sizeof(std::uint32_t) * 4};

    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 2;
    vi.pVertexBindingDescriptions = bindings;
    vi.vertexAttributeDescriptionCount = 9;
    vi.pVertexAttributeDescriptions = attrs2;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineRasterizationStateCreateInfo rs;
    VkPipelineMultisampleStateCreateInfo   ms;
    VkPipelineColorBlendAttachmentState    cb;
    VkPipelineColorBlendStateCreateInfo    cbs;
    VkPipelineViewportStateCreateInfo      vp;
    fillStandardState(rs, ms, cb, cbs, vp);

    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn = {};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkPipelineRenderingCreateInfo rci = {};
    rci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rci.colorAttachmentCount = 1;
    rci.pColorAttachmentFormats = &colorFormat;
    rci.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo gc = {};
    gc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gc.pNext = &rci;
    gc.stageCount = static_cast<std::uint32_t>(stages.size());
    gc.pStages = stages.data();
    gc.pVertexInputState = &vi;
    gc.pInputAssemblyState = &ia;
    gc.pViewportState = &vp;
    gc.pRasterizationState = &rs;
    gc.pMultisampleState = &ms;
    gc.pDepthStencilState = &ds;
    gc.pColorBlendState = &cbs;
    gc.pDynamicState = &dyn;
    gc.layout = opaqueSkinnedLayout_;

    VkPipeline pipe = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gc, nullptr, &pipe));
    return pipe;
}

VkPipeline VulkanPipelines::buildBackgroundPipeline(VkDevice device,
                                                    VkShaderModule vs,
                                                    VkShaderModule fs,
                                                    VkFormat colorFormat,
                                                    VkFormat depthFormat) {
    std::array<VkPipelineShaderStageCreateInfo, 2> stages = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs; stages[1].pName = "main";

    // Vertex pull — the fullscreen-triangle vert derives positions
    // from gl_VertexIndex, so no buffers, no attributes, no bindings.
    VkPipelineVertexInputStateCreateInfo vi = {};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia = {};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineRasterizationStateCreateInfo rs;
    VkPipelineMultisampleStateCreateInfo   ms;
    VkPipelineColorBlendAttachmentState    cb;
    VkPipelineColorBlendStateCreateInfo    cbs;
    VkPipelineViewportStateCreateInfo      vp;
    fillStandardState(rs, ms, cb, cbs, vp);
    rs.cullMode = VK_CULL_MODE_NONE;   // triangle's CCW vs CW depends on viewport Y-flip; skip the dance.

    // M4.8 — straight-alpha blending. Background images are opaque
    // (alpha=255 padded by the host) so this is a no-op for the
    // background draw, but the same pipeline is also used by the
    // foreground sprite layer (see VulkanRenderer's `foreground*`
    // path) which carries proper transparency via the SHP sprite
    // alpha channel.
    cb.blendEnable         = VK_TRUE;
    cb.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cb.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cb.colorBlendOp        = VK_BLEND_OP_ADD;
    cb.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cb.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cb.alphaBlendOp        = VK_BLEND_OP_ADD;

    // Depth test off, write off — the background sits behind everything
    // and must not occlude (or be occluded by) the cleared depth.
    VkPipelineDepthStencilStateCreateInfo ds = {};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable  = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;
    ds.depthCompareOp   = VK_COMPARE_OP_ALWAYS;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn = {};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkPipelineRenderingCreateInfo rci = {};
    rci.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rci.colorAttachmentCount = 1;
    rci.pColorAttachmentFormats = &colorFormat;
    rci.depthAttachmentFormat = depthFormat;

    VkGraphicsPipelineCreateInfo gc = {};
    gc.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gc.pNext = &rci;
    gc.stageCount = static_cast<std::uint32_t>(stages.size());
    gc.pStages = stages.data();
    gc.pVertexInputState = &vi;
    gc.pInputAssemblyState = &ia;
    gc.pViewportState = &vp;
    gc.pRasterizationState = &rs;
    gc.pMultisampleState = &ms;
    gc.pDepthStencilState = &ds;
    gc.pColorBlendState = &cbs;
    gc.pDynamicState = &dyn;
    gc.layout = backgroundLayout_;

    VkPipeline pipe = VK_NULL_HANDLE;
    VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gc, nullptr, &pipe));
    return pipe;
}

bool VulkanPipelines::create(VulkanContext& ctx,
                             VkFormat colorFormat,
                             VkFormat depthFormat,
                             ShaderLoader& shaderLoader,
                             threadmaxx::Engine& engine) {
    const VkDevice device = ctx.device();
    colorFormat_ = colorFormat;
    depthFormat_ = depthFormat;

    // §3.11 batch 9b.3 — register every stage with the loader. The
    // initial bytes are the embedded SPIR-V baked at build time; the
    // disk path is the runtime location of the corresponding .spv
    // file, which the loader re-reads on hot reload.
    for (std::size_t i = 0; i < static_cast<std::size_t>(PipelineShaderSlot::Count); ++i) {
        const auto slot = static_cast<PipelineShaderSlot>(i);
        shaders_[i] = shaderLoader.add(engine, shaderPathFor(slot), embeddedSpvFor(slot));
    }

    // ----------------- Opaque pipeline ---------------------------------------
    {
        VkPushConstantRange pcRange = {};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.size = sizeof(OpaquePushConstants);

        VkPipelineLayoutCreateInfo lc = {};
        lc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        lc.pushConstantRangeCount = 1;
        lc.pPushConstantRanges = &pcRange;
        VK_CHECK(vkCreatePipelineLayout(device, &lc, nullptr, &opaqueLayout_));

        VkShaderModule vs = makeShader(device, embeddedSpvFor(PipelineShaderSlot::OpaqueVert));
        VkShaderModule fs = makeShader(device, embeddedSpvFor(PipelineShaderSlot::OpaqueFrag));
        opaquePipe_ = buildOpaquePipeline(device, vs, fs, colorFormat, depthFormat);
        vkDestroyShaderModule(device, vs, nullptr);
        vkDestroyShaderModule(device, fs, nullptr);
    }

    // ----------------- Debug pipelines (lines + points) ---------------------
    {
        VkPushConstantRange pcRange = {};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcRange.size = sizeof(DebugPushConstants);

        VkPipelineLayoutCreateInfo lc = {};
        lc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        lc.pushConstantRangeCount = 1;
        lc.pPushConstantRanges = &pcRange;
        VK_CHECK(vkCreatePipelineLayout(device, &lc, nullptr, &debugLayout_));

        {
            VkShaderModule vs = makeShader(device, embeddedSpvFor(PipelineShaderSlot::DebugLineVert));
            VkShaderModule fs = makeShader(device, embeddedSpvFor(PipelineShaderSlot::DebugLineFrag));
            debugLinePipe_ = buildDebugLinePipeline(device, vs, fs, colorFormat, depthFormat);
            vkDestroyShaderModule(device, vs, nullptr);
            vkDestroyShaderModule(device, fs, nullptr);
        }

        {
            VkShaderModule vs = makeShader(device, embeddedSpvFor(PipelineShaderSlot::DebugPointVert));
            VkShaderModule fs = makeShader(device, embeddedSpvFor(PipelineShaderSlot::DebugPointFrag));
            debugPointPipe_ = buildDebugPointPipeline(device, vs, fs, colorFormat, depthFormat);
            vkDestroyShaderModule(device, vs, nullptr);
            vkDestroyShaderModule(device, fs, nullptr);
        }
    }

    // ----------------- Opaque skinned pipeline -------------------------
    //
    // §3.11.7b.5 batch 9b.4.a — build the skinned pipeline state.
    // Layout = same push-constants as opaque (viewProj/light/camera)
    // PLUS a descriptor set 0 holding the bone matrix SSBO at
    // binding 0. The actual descriptor set ALLOCATION + buffer
    // binding lives in 9b.4.b's renderer-side per-frame upload path;
    // here we just stand up the layout so the pipeline is valid.
    {
        VkDescriptorSetLayoutBinding boneBinding = {};
        boneBinding.binding         = 0;
        boneBinding.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        boneBinding.descriptorCount = 1;
        boneBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo dsl = {};
        dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsl.bindingCount = 1;
        dsl.pBindings    = &boneBinding;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &dsl, nullptr,
                                             &opaqueSkinnedBoneSetLayout_));

        VkPushConstantRange pcRange = {};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.size = sizeof(OpaquePushConstants);

        VkPipelineLayoutCreateInfo lc = {};
        lc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        lc.setLayoutCount = 1;
        lc.pSetLayouts    = &opaqueSkinnedBoneSetLayout_;
        lc.pushConstantRangeCount = 1;
        lc.pPushConstantRanges    = &pcRange;
        VK_CHECK(vkCreatePipelineLayout(device, &lc, nullptr,
                                        &opaqueSkinnedLayout_));

        VkShaderModule vs = makeShader(device,
            embeddedSpvFor(PipelineShaderSlot::OpaqueSkinnedVert));
        VkShaderModule fs = makeShader(device,
            embeddedSpvFor(PipelineShaderSlot::OpaqueFrag));
        opaqueSkinnedPipe_ = buildOpaqueSkinnedPipeline(
            device, vs, fs, colorFormat, depthFormat);
        vkDestroyShaderModule(device, vs, nullptr);
        vkDestroyShaderModule(device, fs, nullptr);
    }

    // ----------------- Background pipeline -----------------------------
    //
    // M2.8 — single combined-image-sampler at set 0, binding 0. The
    // descriptor pool + per-instance set allocation lives in the
    // renderer's `setBackgroundFromRgba` path; here we just stand up
    // the layout + pipeline so they're ready when game code calls in.
    {
        VkDescriptorSetLayoutBinding texBinding = {};
        texBinding.binding         = 0;
        texBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        texBinding.descriptorCount = 1;
        texBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo dsl = {};
        dsl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsl.bindingCount = 1;
        dsl.pBindings    = &texBinding;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &dsl, nullptr,
                                             &backgroundSetLayout_));

        // Push constant range = mat4 viewProj + vec4 worldHalfExtent
        // (80 B total, well under the 128 B portable minimum).
        VkPushConstantRange pcRange = {};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcRange.size       = sizeof(float) * (16 + 4);

        VkPipelineLayoutCreateInfo lc = {};
        lc.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        lc.setLayoutCount         = 1;
        lc.pSetLayouts            = &backgroundSetLayout_;
        lc.pushConstantRangeCount = 1;
        lc.pPushConstantRanges    = &pcRange;
        VK_CHECK(vkCreatePipelineLayout(device, &lc, nullptr,
                                        &backgroundLayout_));

        VkShaderModule vs = makeShader(device,
            embeddedSpvFor(PipelineShaderSlot::BackgroundVert));
        VkShaderModule fs = makeShader(device,
            embeddedSpvFor(PipelineShaderSlot::BackgroundFrag));
        backgroundPipe_ = buildBackgroundPipeline(
            device, vs, fs, colorFormat, depthFormat);
        vkDestroyShaderModule(device, vs, nullptr);
        vkDestroyShaderModule(device, fs, nullptr);
    }

    return true;
}

bool VulkanPipelines::recreatePipelines(VulkanContext& ctx,
                                        VkFormat colorFormat,
                                        VkFormat depthFormat,
                                        threadmaxx::Engine& engine) {
    colorFormat_ = colorFormat;
    depthFormat_ = depthFormat;

    // The layouts survive a swapchain recreate (push-constant ranges
    // and descriptor sets are layout-independent of the swapchain
    // format). Only the VkPipeline objects need fresh builds.
    if (!rebuildOpaque(ctx, engine))         return false;
    if (!rebuildDebugLine(ctx, engine))      return false;
    if (!rebuildDebugPoint(ctx, engine))     return false;
    if (!rebuildOpaqueSkinned(ctx, engine))  return false;
    if (!rebuildBackground(ctx, engine))     return false;
    return true;
}

void VulkanPipelines::destroy(VulkanContext& ctx) noexcept {
    // M2.8 — tear down background state.
    if (backgroundPipe_)      { vkDestroyPipeline(ctx.device(), backgroundPipe_, nullptr); backgroundPipe_ = VK_NULL_HANDLE; }
    if (backgroundLayout_)    { vkDestroyPipelineLayout(ctx.device(), backgroundLayout_, nullptr); backgroundLayout_ = VK_NULL_HANDLE; }
    if (backgroundSetLayout_) { vkDestroyDescriptorSetLayout(ctx.device(), backgroundSetLayout_, nullptr); backgroundSetLayout_ = VK_NULL_HANDLE; }

    // §3.11.7b.5 batch 9b.4.a — tear down skinned state.
    if (opaqueSkinnedPipe_)          { vkDestroyPipeline(ctx.device(), opaqueSkinnedPipe_, nullptr); opaqueSkinnedPipe_ = VK_NULL_HANDLE; }
    if (opaqueSkinnedLayout_)        { vkDestroyPipelineLayout(ctx.device(), opaqueSkinnedLayout_, nullptr); opaqueSkinnedLayout_ = VK_NULL_HANDLE; }
    if (opaqueSkinnedBoneSetLayout_) { vkDestroyDescriptorSetLayout(ctx.device(), opaqueSkinnedBoneSetLayout_, nullptr); opaqueSkinnedBoneSetLayout_ = VK_NULL_HANDLE; }

    if (debugPointPipe_)  { vkDestroyPipeline(ctx.device(), debugPointPipe_, nullptr); debugPointPipe_ = VK_NULL_HANDLE; }
    if (debugLinePipe_)   { vkDestroyPipeline(ctx.device(), debugLinePipe_, nullptr);  debugLinePipe_  = VK_NULL_HANDLE; }
    if (debugLayout_)     { vkDestroyPipelineLayout(ctx.device(), debugLayout_, nullptr); debugLayout_ = VK_NULL_HANDLE; }

    if (opaquePipe_)      { vkDestroyPipeline(ctx.device(), opaquePipe_, nullptr); opaquePipe_ = VK_NULL_HANDLE; }
    if (opaqueLayout_)    { vkDestroyPipelineLayout(ctx.device(), opaqueLayout_, nullptr); opaqueLayout_ = VK_NULL_HANDLE; }

    // §3.11 batch 9b.3 — drop shader refcounts so the loader's slots
    // free if nothing else holds them. The loader's own teardown is
    // independent.
    for (auto& h : shaders_) h.reset();
}

threadmaxx::ResourceId<Shader> VulkanPipelines::shaderId(PipelineShaderSlot slot) const noexcept {
    const auto i = static_cast<std::size_t>(slot);
    if (i >= shaders_.size()) return {};
    return shaders_[i].id();
}

bool VulkanPipelines::rebuildOpaque(VulkanContext& ctx, threadmaxx::Engine& engine) {
    const auto vsSpv = spvFromRegistry(engine,
                                       shaders_[static_cast<std::size_t>(
                                           PipelineShaderSlot::OpaqueVert)].id());
    const auto fsSpv = spvFromRegistry(engine,
                                       shaders_[static_cast<std::size_t>(
                                           PipelineShaderSlot::OpaqueFrag)].id());
    if (vsSpv.empty() || fsSpv.empty()) return false;

    const VkDevice device = ctx.device();
    VkShaderModule vs = makeShader(device, vsSpv);
    VkShaderModule fs = makeShader(device, fsSpv);

    VkPipeline fresh = buildOpaquePipeline(device, vs, fs, colorFormat_, depthFormat_);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);
    if (fresh == VK_NULL_HANDLE) return false;

    if (opaquePipe_) vkDestroyPipeline(device, opaquePipe_, nullptr);
    opaquePipe_ = fresh;
    return true;
}

bool VulkanPipelines::rebuildDebugLine(VulkanContext& ctx, threadmaxx::Engine& engine) {
    const auto vsSpv = spvFromRegistry(engine,
                                       shaders_[static_cast<std::size_t>(
                                           PipelineShaderSlot::DebugLineVert)].id());
    const auto fsSpv = spvFromRegistry(engine,
                                       shaders_[static_cast<std::size_t>(
                                           PipelineShaderSlot::DebugLineFrag)].id());
    if (vsSpv.empty() || fsSpv.empty()) return false;

    const VkDevice device = ctx.device();
    VkShaderModule vs = makeShader(device, vsSpv);
    VkShaderModule fs = makeShader(device, fsSpv);

    VkPipeline fresh = buildDebugLinePipeline(device, vs, fs, colorFormat_, depthFormat_);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);
    if (fresh == VK_NULL_HANDLE) return false;

    if (debugLinePipe_) vkDestroyPipeline(device, debugLinePipe_, nullptr);
    debugLinePipe_ = fresh;
    return true;
}

bool VulkanPipelines::rebuildDebugPoint(VulkanContext& ctx, threadmaxx::Engine& engine) {
    const auto vsSpv = spvFromRegistry(engine,
                                       shaders_[static_cast<std::size_t>(
                                           PipelineShaderSlot::DebugPointVert)].id());
    const auto fsSpv = spvFromRegistry(engine,
                                       shaders_[static_cast<std::size_t>(
                                           PipelineShaderSlot::DebugPointFrag)].id());
    if (vsSpv.empty() || fsSpv.empty()) return false;

    const VkDevice device = ctx.device();
    VkShaderModule vs = makeShader(device, vsSpv);
    VkShaderModule fs = makeShader(device, fsSpv);

    VkPipeline fresh = buildDebugPointPipeline(device, vs, fs, colorFormat_, depthFormat_);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);
    if (fresh == VK_NULL_HANDLE) return false;

    if (debugPointPipe_) vkDestroyPipeline(device, debugPointPipe_, nullptr);
    debugPointPipe_ = fresh;
    return true;
}

bool VulkanPipelines::rebuildOpaqueSkinned(VulkanContext& ctx, threadmaxx::Engine& engine) {
    const auto vsSpv = spvFromRegistry(engine,
                                       shaders_[static_cast<std::size_t>(
                                           PipelineShaderSlot::OpaqueSkinnedVert)].id());
    // The skinned pipeline shares its fragment stage with the
    // non-skinned opaque pipeline (same `vNormal` + `vColor`
    // inputs); pull the current `OpaqueFrag` bytes from the registry.
    const auto fsSpv = spvFromRegistry(engine,
                                       shaders_[static_cast<std::size_t>(
                                           PipelineShaderSlot::OpaqueFrag)].id());
    if (vsSpv.empty() || fsSpv.empty()) return false;

    const VkDevice device = ctx.device();
    VkShaderModule vs = makeShader(device, vsSpv);
    VkShaderModule fs = makeShader(device, fsSpv);

    VkPipeline fresh = buildOpaqueSkinnedPipeline(device, vs, fs, colorFormat_, depthFormat_);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);
    if (fresh == VK_NULL_HANDLE) return false;

    if (opaqueSkinnedPipe_) vkDestroyPipeline(device, opaqueSkinnedPipe_, nullptr);
    opaqueSkinnedPipe_ = fresh;
    return true;
}

bool VulkanPipelines::rebuildBackground(VulkanContext& ctx, threadmaxx::Engine& engine) {
    const auto vsSpv = spvFromRegistry(engine,
                                       shaders_[static_cast<std::size_t>(
                                           PipelineShaderSlot::BackgroundVert)].id());
    const auto fsSpv = spvFromRegistry(engine,
                                       shaders_[static_cast<std::size_t>(
                                           PipelineShaderSlot::BackgroundFrag)].id());
    if (vsSpv.empty() || fsSpv.empty()) return false;

    const VkDevice device = ctx.device();
    VkShaderModule vs = makeShader(device, vsSpv);
    VkShaderModule fs = makeShader(device, fsSpv);

    VkPipeline fresh = buildBackgroundPipeline(device, vs, fs, colorFormat_, depthFormat_);
    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);
    if (fresh == VK_NULL_HANDLE) return false;

    if (backgroundPipe_) vkDestroyPipeline(device, backgroundPipe_, nullptr);
    backgroundPipe_ = fresh;
    return true;
}

bool VulkanPipelines::rebuildIfMatches(VulkanContext& ctx,
                                       threadmaxx::Engine& engine,
                                       threadmaxx::ResourceId<Shader> oldId,
                                       threadmaxx::ResourceId<Shader> newId) {
    // Identify which slot the oldId belonged to.
    int slotIdx = -1;
    for (std::size_t i = 0; i < shaders_.size(); ++i) {
        if (shaders_[i].id() == oldId) { slotIdx = static_cast<int>(i); break; }
    }
    if (slotIdx < 0) return false;

    // Re-acquire the new slot via the registry so the refcount pin
    // survives. The old handle drops when overwritten below.
    auto newHandle = engine.resources().acquire<Shader>(newId);
    if (!newHandle.valid()) return false;

    shaders_[static_cast<std::size_t>(slotIdx)] = std::move(newHandle);

    // Wait for any in-flight frame using the affected pipeline to
    // complete before we destroy it. Hot reload isn't a hot path —
    // the device-wide stall is acceptable, and keeps the lifetime
    // story simple.
    if (ctx.device()) vkDeviceWaitIdle(ctx.device());

    const auto slot = static_cast<PipelineShaderSlot>(slotIdx);
    switch (slot) {
        case PipelineShaderSlot::OpaqueVert:
            return rebuildOpaque(ctx, engine);
        case PipelineShaderSlot::OpaqueFrag:
            // The fragment shader is shared between the opaque and
            // opaque-skinned pipelines, so a reload here has to
            // rebuild BOTH. Order doesn't matter — both calls
            // pull fresh SPIR-V from the same registry entry.
            return rebuildOpaque(ctx, engine) &&
                   rebuildOpaqueSkinned(ctx, engine);
        case PipelineShaderSlot::OpaqueSkinnedVert:
            return rebuildOpaqueSkinned(ctx, engine);
        case PipelineShaderSlot::DebugLineVert:
        case PipelineShaderSlot::DebugLineFrag:
            return rebuildDebugLine(ctx, engine);
        case PipelineShaderSlot::DebugPointVert:
        case PipelineShaderSlot::DebugPointFrag:
            return rebuildDebugPoint(ctx, engine);
        case PipelineShaderSlot::BackgroundVert:
        case PipelineShaderSlot::BackgroundFrag:
            return rebuildBackground(ctx, engine);
        case PipelineShaderSlot::Count:
            return false;
    }
    return false;
}

} // namespace threadmaxx_vk
