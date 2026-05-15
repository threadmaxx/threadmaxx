#include "VulkanPipelines.hpp"

#include "VkUtil.hpp"

#include "opaque_vert_spv.hpp"
#include "opaque_frag_spv.hpp"
#include "debug_line_vert_spv.hpp"
#include "debug_line_frag_spv.hpp"
#include "debug_point_vert_spv.hpp"
#include "debug_point_frag_spv.hpp"

#include <array>

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

} // namespace

bool VulkanPipelines::create(VulkanContext& ctx,
                             VkFormat colorFormat,
                             VkFormat depthFormat) {
    const VkDevice device = ctx.device();

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

        VkShaderModule vs = makeShader(device, k_opaque_vert_spv);
        VkShaderModule fs = makeShader(device, k_opaque_frag_spv);

        std::array<VkPipelineShaderStageCreateInfo, 2> stages = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs;
        stages[1].pName = "main";

        // Per-vertex binding (cube mesh, 24 bytes: pos[3] + normal[3]).
        VkVertexInputBindingDescription bindings[2] = {};
        bindings[0].binding = 0;
        bindings[0].stride = sizeof(float) * 6;
        bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        // Per-instance binding (InstanceLayoutEntry, 128 bytes).
        bindings[1].binding = 1;
        bindings[1].stride = 128;
        bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;

        // Only the attributes the shader consumes are declared. Locations
        // 6 (instMeshMat) and 7 (instFlagsSort) are reserved in the
        // InstanceLayoutEntry layout but not read by the current opaque
        // shader; declaring them anyway makes validation complain. They
        // come back when the shader starts using mesh/material ids.
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

        VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gc, nullptr, &opaquePipe_));

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

        // Lines: per-vertex pos + color
        {
            VkShaderModule vs = makeShader(device, k_debug_line_vert_spv);
            VkShaderModule fs = makeShader(device, k_debug_line_frag_spv);
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
            VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gc, nullptr, &debugLinePipe_));

            vkDestroyShaderModule(device, vs, nullptr);
            vkDestroyShaderModule(device, fs, nullptr);
        }

        // Points: per-vertex pos + color + pixelSize
        {
            VkShaderModule vs = makeShader(device, k_debug_point_vert_spv);
            VkShaderModule fs = makeShader(device, k_debug_point_frag_spv);
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
            VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gc, nullptr, &debugPointPipe_));

            vkDestroyShaderModule(device, vs, nullptr);
            vkDestroyShaderModule(device, fs, nullptr);
        }
    }

    return true;
}

void VulkanPipelines::destroy(VulkanContext& ctx) noexcept {
    if (debugPointPipe_)  { vkDestroyPipeline(ctx.device(), debugPointPipe_, nullptr); debugPointPipe_ = VK_NULL_HANDLE; }
    if (debugLinePipe_)   { vkDestroyPipeline(ctx.device(), debugLinePipe_, nullptr);  debugLinePipe_  = VK_NULL_HANDLE; }
    if (debugLayout_)     { vkDestroyPipelineLayout(ctx.device(), debugLayout_, nullptr); debugLayout_ = VK_NULL_HANDLE; }

    if (opaquePipe_)      { vkDestroyPipeline(ctx.device(), opaquePipe_, nullptr); opaquePipe_ = VK_NULL_HANDLE; }
    if (opaqueLayout_)    { vkDestroyPipelineLayout(ctx.device(), opaqueLayout_, nullptr); opaqueLayout_ = VK_NULL_HANDLE; }
}

} // namespace threadmaxx_vk
