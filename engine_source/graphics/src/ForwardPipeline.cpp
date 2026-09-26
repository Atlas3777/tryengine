#include "engine/graphics/ForwardPipeline.hpp"

#include <algorithm>
#include <cstring>

#include "engine/graphics/OpaqueGeometryPass.hpp"

namespace tryengine::graphics {

void SetupFrameConstants(RenderGraph& rg, const CameraData& camera) {
    struct LightUBO {
        hlslpp::float4 am_color{0.1f, 0.1f, 0.1f, 0.0f};
        hlslpp::float4 view_pos;
    } light_ubo;
    light_ubo.view_pos = hlslpp::float4(camera.position, 0.0f);

    rg.PutCPUData(RGTag_v<"GlobalLightUBO">, light_ubo);
    rg.PutCPUData(RGTag_v<"Camera">, camera);
}

RGResourceHandle AddLightPass(RenderGraph& rg, eastl::span<const PointLightGPU> point_lights) {
    auto& light_data = rg.AddPass<LightPass>(
        "LightPass",
        [point_lights](RenderGraphBuilder& builder, LightPass& data) {
            data.point_lights_queue = point_lights;

            RGBufferDesc desc;
            desc.debug_name = "PointLightBuffer";
            desc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
            desc.memory_usage = VMA_MEMORY_USAGE_AUTO;
            const size_t element_count = std::max<size_t>(1, data.point_lights_queue.size());
            desc.size = sizeof(PointLightGPU) * element_count;

            data.buffer = builder.CreateBuffer("PointLightBuffer", desc);
            builder.Write(data.buffer, RGUsageHint::StorageWrite);
            builder.Export(RGTag_v<"PointLightBuffer">, data.buffer);
        },
        [](RGExecuteContext& ctx, const LightPass& data) {
            if (data.point_lights_queue.empty()) {
                return;
            }

            VkBuffer dst_buffer = ctx.GetBuffer(data.buffer);
            if (!dst_buffer) {
                return;
            }

            const VkDeviceSize upload_size = sizeof(PointLightGPU) * data.point_lights_queue.size();

            // Обновляем UBO/SSBO через vkCmdUpdateBuffer для пакетов до 64 КБ
            vkCmdUpdateBuffer(ctx.cmd_buffer, dst_buffer, 0, upload_size, data.point_lights_queue.data());
        });

    return light_data.buffer;
}

ForwardPipelineOutput BuildForwardPipeline(
    RenderGraph& rg,
    const FrameRenderData& frame_data,
    uint32_t width,
    uint32_t height
) {
    ForwardPipelineOutput out;

    out.light_buffer = AddLightPass(rg, frame_data.point_lights);

    auto& forward_data = rg.AddPass<OpaquePass>(
        "OpaquePass",
        [&, width, height](RenderGraphBuilder& builder, OpaquePass& data) {
            RGTextureDesc scene_color_desc{
                .width = width,
                .height = height,
                .layers = 1,
                .mips = 1,
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                .debug_name = "SceneColor"
            };

            RGTextureDesc scene_depth_desc{
                .width = width,
                .height = height,
                .layers = 1,
                .mips = 1,
                .format = VK_FORMAT_D32_SFLOAT,
                .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                .aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .debug_name = "SceneDepth"
            };

            RGResourceHandle color_rt = builder.CreateTexture("SceneColor", scene_color_desc);
            RGResourceHandle depth_rt = builder.CreateTexture("SceneDepth", scene_depth_desc);

            data.color_target = builder.Write(color_rt, RGUsageHint::ColorAttachment);
            data.depth_target = builder.Write(depth_rt, RGUsageHint::DepthStencilAttachment);
            data.light_buffer = builder.Read(out.light_buffer, RGUsageHint::ShaderRead);

            data.opaque_pass_queue = eastl::span(frame_data.opaque_queue.data(), frame_data.opaque_queue.size());

            builder.Export(RGTag_v<"SceneViewport">, data.color_target);
        },
        [width, height](RGExecuteContext& ctx, const OpaquePass& data) {
            //vkCmdPushDescriptorSetKHR //TODO: создать в RenderGraph Frame buffer и пушить set 0

            RGColorAttachment color_attachment{};
            color_attachment.handle = data.color_target;
            color_attachment.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color_attachment.store_op = VK_ATTACHMENT_STORE_OP_STORE;
            color_attachment.clear_value.color = {{0.1f, 0.1f, 0.12f, 1.0f}};

            RGDepthStencilAttachment depth_attachment{};
            depth_attachment.handle = data.depth_target;
            depth_attachment.depth_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depth_attachment.depth_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depth_attachment.clear_value.depthStencil = {1.0f, 0};

            VkRect2D render_area{};
            render_area.offset = {0, 0};
            render_area.extent = {width, height};

            ctx.BeginRendering({&color_attachment, 1}, &depth_attachment, render_area);

            VkViewport viewport{
                .x = 0.0f,
                .y = 0.0f,
                .width = static_cast<float>(width),
                .height = static_cast<float>(height),
                .minDepth = 0.0f,
                .maxDepth = 1.0f
            };
            vkCmdSetViewportWithCountEXT(ctx.cmd_buffer, 1, &viewport);

            VkRect2D scissor = render_area;
            vkCmdSetScissorWithCountEXT(ctx.cmd_buffer, 1, &scissor);

            OpaqueGeometryPass::ExecuteDrawCommands(ctx, data.opaque_pass_queue);

            ctx.EndRendering();
        });

    out.color_target = forward_data.color_target;
    out.depth_target = forward_data.depth_target;

    return out;
}


}  // namespace tryengine::graphics