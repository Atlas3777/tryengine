#pragma once

#include "editor/CollectDebug.hpp"
#include "engine/graphics/PipelineManager.hpp"
#include "engine/graphics/RenderCommon.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryeditor {

struct DebugDrawPassData {
    RGResourceHandle color_target;
    RGResourceHandle depth_target;
    eastl::span<const DebugLine> lines;
    tryengine::resources::ResourceHandle<tryengine::graphics::Shader> shader;
};

struct alignas(16) CameraGPU {
    hlslpp::float4x4 view;
    hlslpp::float4x4 proj;
};

struct DebugPushConstants {
    VkDeviceAddress lines;      // 8 байт
    VkDeviceAddress camera;     // 8 байт
    float screen_width;         // 4 байта
    float screen_height;        // 4 байта
    float thickness;            // 4 байта
    uint32_t _pad;              // 4 байта -> Итого 32 байта
};

inline void AddDebugDrawPass(
    tryengine::graphics::RenderGraph& rg,
    RGResourceHandle color_target,
    RGResourceHandle depth_target,
    eastl::span<const DebugLine> lines,
    tryengine::resources::ResourceHandle<tryengine::graphics::Shader> debug_shader,
    uint32_t width,
    uint32_t height
) {
    if (lines.empty() || !debug_shader) {
        return;
    }

    rg.AddPass<DebugDrawPassData>(
        "DebugDrawPass",
        [&](tryengine::graphics::RenderGraphBuilder& builder, DebugDrawPassData& data) {
            data.color_target = builder.Write(color_target, tryengine::graphics::RGUsageHint::ColorAttachment);
            data.depth_target = builder.Write(depth_target, tryengine::graphics::RGUsageHint::DepthStencilAttachment);
            data.lines = lines;
            data.shader = debug_shader;
        },
        [width, height](tryengine::graphics::RGExecuteContext& ctx, const DebugDrawPassData& data) {
            const auto* camera_cpu = ctx.cpu_bb->GetAs<tryengine::graphics::CameraData>(tryengine::graphics::RGTag_v<"Camera">);
            TRY_ASSERT(camera_cpu, "Camera missing in CPUBlackboard!");

            // 1. BDA-выделение транзиентной памяти
            CameraGPU camera_gpu{
                .view = camera_cpu->view,
                .proj = camera_cpu->proj
            };
            auto camera_alloc = ctx.AllocateTransient(&camera_gpu, sizeof(CameraGPU), alignof(CameraGPU));

            const size_t lines_size_bytes = sizeof(DebugLine) * data.lines.size();
            auto lines_alloc = ctx.AllocateTransient(data.lines.data(), lines_size_bytes, alignof(DebugLine));

            // 2. Attachments
            tryengine::graphics::RGColorAttachment color_att{};
            color_att.handle = data.color_target;
            color_att.load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
            color_att.store_op = VK_ATTACHMENT_STORE_OP_STORE;

            tryengine::graphics::RGDepthStencilAttachment depth_att{};
            depth_att.handle = data.depth_target;
            depth_att.depth_load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
            depth_att.depth_store_op = VK_ATTACHMENT_STORE_OP_STORE;

            VkRect2D render_area{{0, 0}, {width, height}};
            ctx.BeginRendering(eastl::span(&color_att, 1), &depth_att, render_area);

            VkViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
            vkCmdSetViewportWithCountEXT(ctx.cmd_buffer, 1, &viewport);
            vkCmdSetScissorWithCountEXT(ctx.cmd_buffer, 1, &render_area);

            // 3. Dynamic States
            vkCmdSetRasterizerDiscardEnableEXT(ctx.cmd_buffer, VK_FALSE);
            vkCmdSetCullModeEXT(ctx.cmd_buffer, VK_CULL_MODE_NONE);
            vkCmdSetFrontFaceEXT(ctx.cmd_buffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
            vkCmdSetPrimitiveTopologyEXT(ctx.cmd_buffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            vkCmdSetPolygonModeEXT(ctx.cmd_buffer, VK_POLYGON_MODE_FILL);
            vkCmdSetPrimitiveRestartEnableEXT(ctx.cmd_buffer, VK_FALSE);

            vkCmdSetVertexInputEXT(ctx.cmd_buffer, 0, nullptr, 0, nullptr);

            // Тест глубины включен, но запись отключена
            vkCmdSetDepthTestEnableEXT(ctx.cmd_buffer, VK_TRUE);
            vkCmdSetDepthWriteEnableEXT(ctx.cmd_buffer, VK_FALSE);
            vkCmdSetDepthCompareOpEXT(ctx.cmd_buffer, VK_COMPARE_OP_LESS_OR_EQUAL);
            vkCmdSetDepthBiasEnableEXT(ctx.cmd_buffer, VK_FALSE);
            vkCmdSetStencilTestEnableEXT(ctx.cmd_buffer, VK_FALSE);

            // Multisampling & Blending
            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
            vkCmdSetRasterizationSamplesEXT(ctx.cmd_buffer, samples);
            VkSampleMask sample_mask = 0xFFFFFFFF;
            vkCmdSetSampleMaskEXT(ctx.cmd_buffer, samples, &sample_mask);
            vkCmdSetAlphaToCoverageEnableEXT(ctx.cmd_buffer, VK_FALSE);

            constexpr VkBool32 blend_enables[] = {VK_FALSE};
            vkCmdSetColorBlendEnableEXT(ctx.cmd_buffer, 0, 1, blend_enables);
            constexpr VkColorComponentFlags color_write_masks[] = {
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
            };
            vkCmdSetColorWriteMaskEXT(ctx.cmd_buffer, 0, 1, color_write_masks);
            constexpr VkBool32 color_write_enables[] = {VK_TRUE};
            vkCmdSetColorWriteEnableEXT(ctx.cmd_buffer, 1, color_write_enables);

            // 4. Bind Shader
            tryengine::graphics::Shader* shader = data.shader.Get();
            VkShaderStageFlagBits stages[] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
            VkShaderEXT shaders[] = {shader->vertex_shader, shader->fragment_shader};
            vkCmdBindShadersEXT(ctx.cmd_buffer, 2, stages, shaders);

            // 5. Push Constants
            DebugPushConstants pc{
                .lines = lines_alloc.bda_address,
                .camera = camera_alloc.bda_address,
                .screen_width = static_cast<float>(width),
                .screen_height = static_cast<float>(height),
                .thickness = 4.0f,
                ._pad = 0
            };

            vkCmdPushConstants(
                ctx.cmd_buffer,
                shader->layout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(DebugPushConstants),
                &pc
            );

            // 6. Draw Call
            const uint32_t total_vertices = static_cast<uint32_t>(data.lines.size() * 6);
            vkCmdDraw(ctx.cmd_buffer, total_vertices, 1, 0, 0);

            ctx.EndRendering();
        }
    );
}

} // namespace tryeditor