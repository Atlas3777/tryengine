#pragma once

#include <volk.h>

namespace tryengine::graphics {

// Общий каркас динамического состояния Shader Objects для Opaque/Picking/Debug.
// Раньше этот блок на ~40 строк был скопирован в каждый пасс.
struct RasterState {
    VkCullModeFlags cull_mode = VK_CULL_MODE_BACK_BIT;
    bool depth_test = true;
    bool depth_write = true;
    VkCompareOp depth_compare = VK_COMPARE_OP_LESS;
    VkColorComponentFlags color_write_mask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    // Blending is off by default so opaque passes keep their current state; translucent
    // passes (grid, gizmo, UI) enable it and set their factors explicitly.
    bool blend_enable = false;
    VkBlendFactor src_color = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dst_color = VK_BLEND_FACTOR_ZERO;
    VkBlendFactor src_alpha = VK_BLEND_FACTOR_ONE;
    VkBlendFactor dst_alpha = VK_BLEND_FACTOR_ZERO;
};

inline void ApplyRasterState(VkCommandBuffer cmd, const RasterState& s = {}) {
    vkCmdSetRasterizerDiscardEnableEXT(cmd, VK_FALSE);
    vkCmdSetCullModeEXT(cmd, s.cull_mode);
    vkCmdSetFrontFaceEXT(cmd, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetPrimitiveTopologyEXT(cmd, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetPolygonModeEXT(cmd, VK_POLYGON_MODE_FILL);
    vkCmdSetPrimitiveRestartEnableEXT(cmd, VK_FALSE);

    // BDA без стандартных Vertex Buffers
    vkCmdSetVertexInputEXT(cmd, 0, nullptr, 0, nullptr);

    vkCmdSetDepthTestEnableEXT(cmd, s.depth_test ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthWriteEnableEXT(cmd, s.depth_write ? VK_TRUE : VK_FALSE);
    vkCmdSetDepthCompareOpEXT(cmd, s.depth_compare);
    vkCmdSetDepthBiasEnableEXT(cmd, VK_FALSE);
    vkCmdSetStencilTestEnableEXT(cmd, VK_FALSE);

    constexpr VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    constexpr VkSampleMask sample_mask = 0xFFFFFFFF;
    vkCmdSetRasterizationSamplesEXT(cmd, samples);
    vkCmdSetSampleMaskEXT(cmd, samples, &sample_mask);
    vkCmdSetAlphaToCoverageEnableEXT(cmd, VK_FALSE);

    const VkBool32 blend_enable = s.blend_enable ? VK_TRUE : VK_FALSE;
    vkCmdSetColorBlendEnableEXT(cmd, 0, 1, &blend_enable);
    vkCmdSetColorWriteMaskEXT(cmd, 0, 1, &s.color_write_mask);

    const VkColorBlendEquationEXT blend_equation{
        .srcColorBlendFactor = s.src_color,
        .dstColorBlendFactor = s.dst_color,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = s.src_alpha,
        .dstAlphaBlendFactor = s.dst_alpha,
        .alphaBlendOp = VK_BLEND_OP_ADD,
    };
    vkCmdSetColorBlendEquationEXT(cmd, 0, 1, &blend_equation);

    constexpr VkBool32 write_enable = VK_TRUE;
    vkCmdSetColorWriteEnableEXT(cmd, 1, &write_enable);
}

inline void SetViewportScissor(VkCommandBuffer cmd, VkExtent2D extent) {
    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(extent.width), static_cast<float>(extent.height),
                              0.0f, 1.0f};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewportWithCountEXT(cmd, 1, &viewport);
    vkCmdSetScissorWithCountEXT(cmd, 1, &scissor);
}

}  // namespace tryengine::graphics