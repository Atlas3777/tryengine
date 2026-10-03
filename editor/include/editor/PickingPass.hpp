#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <hlsl++/matrix_float_type.h>
#include <algorithm>

#include "EditorRender.hpp"
#include "engine/graphics/MeshDrawCall.hpp"
#include "engine/graphics/PipelineManager.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryeditor {

struct alignas(16) PickingObjectGPU {
    hlslpp::float4x4 model;
    uint64_t entity_id;
    VkDeviceAddress position_buffer_addr;
};

struct PushConstants {
    VkDeviceAddress objects; // BDA указатель на PickingObjectGPU[]
    VkDeviceAddress camera;  // BDA указатель на CameraGPU
};

inline void AddEditorPickingPass(
    tryengine::graphics::RenderGraph& rg,
    tryengine::graphics::RGResourceHandle depth_target,
    tryengine::graphics::RGResourceHandle readback_buffer_handle,
    eastl::span<const tryengine::graphics::MeshDrawCall> draw_queue,
    tryengine::resources::ResourceHandle<tryengine::graphics::Shader> picking_shader,
    uint32_t width,
    uint32_t height,
    uint32_t mouse_x,
    uint32_t mouse_y)
{
    if (!picking_shader || !readback_buffer_handle.IsValid()) return;

    struct PickingPassData {
        tryengine::graphics::RGResourceHandle picking_target;
        tryengine::graphics::RGResourceHandle depth_target;
        tryengine::graphics::RGResourceHandle readback_buffer;
        tryengine::resources::ResourceHandle<tryengine::graphics::Shader> shader;
    };

    rg.AddPass<PickingPassData>(
        "EditorPickingPass",
        [&](tryengine::graphics::RenderGraphBuilder& builder, PickingPassData& data) {
            tryengine::graphics::RGTextureDesc picking_desc{
                .width = width,
                .height = height,
                .layers = 1,
                .mips = 1,
                .format = VK_FORMAT_R32G32_UINT,
                .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                .debug_name = "PickingTexture"
            };

            data.picking_target = builder.CreateTexture("PickingTexture", picking_desc);
            data.picking_target = builder.Write(data.picking_target, tryengine::graphics::RGUsageHint::ColorAttachment);

            data.depth_target = builder.Write(depth_target, tryengine::graphics::RGUsageHint::DepthStencilAttachment);

            data.readback_buffer = builder.Write(readback_buffer_handle, tryengine::graphics::RGUsageHint::TransferDst);

            data.shader = picking_shader;
            builder.MarkSideEffect();
        },
        [width, height, mouse_x, mouse_y, draw_queue](tryengine::graphics::RGExecuteContext& ctx, const PickingPassData& data) {
            // 1. Предварительный отбор только валидных объектов
            eastl::vector<tryengine::graphics::MeshDrawCall> valid_draws;
            valid_draws.reserve(draw_queue.size());
            for (const auto& draw : draw_queue) {
                if (draw.mesh && draw.mesh->position_buffer_addr && draw.mesh->index_buffer) {
                    valid_draws.push_back(draw);
                }
            }

            if (valid_draws.empty()) return;

            // 2. Выделение BDA-памяти под Камеру
            const auto* camera_cpu = ctx.cpu_bb->GetAs<tryengine::graphics::CameraData>(tryengine::graphics::RGTag_v<"Camera">);
            TRY_ASSERT(camera_cpu, "Camera missing in CPUBlackboard!");

            CameraGPU camera_gpu{
                .view = camera_cpu->view,
                .proj = camera_cpu->proj
            };
            auto camera_alloc = ctx.AllocateTransient(&camera_gpu, sizeof(CameraGPU), alignof(CameraGPU));

            // 3. Выделение BDA-памяти сразу под ВСЕ объекты кадра
            auto objects_alloc = ctx.AllocateTransient(
                nullptr,
                sizeof(PickingObjectGPU) * valid_draws.size(),
                alignof(PickingObjectGPU)
            );
            PickingObjectGPU* mapped_objects = reinterpret_cast<PickingObjectGPU*>(objects_alloc.mapped_ptr);

            // 4. Настройка Attachments и начало Rendering
            tryengine::graphics::RGColorAttachment color_att{};
            color_att.handle = data.picking_target;
            color_att.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color_att.store_op = VK_ATTACHMENT_STORE_OP_STORE;
            color_att.clear_value.color.uint32[0] = 0;
            color_att.clear_value.color.uint32[1] = 0;

            tryengine::graphics::RGDepthStencilAttachment depth_att{};
            depth_att.handle = data.depth_target;
            depth_att.depth_load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
            depth_att.depth_store_op = VK_ATTACHMENT_STORE_OP_STORE;

            VkRect2D render_area{{0, 0}, {width, height}};
            ctx.BeginRendering(eastl::span(&color_att, 1), &depth_att, render_area);

            // 5. Настройка динамических состояний
            VkViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
            vkCmdSetViewportWithCountEXT(ctx.cmd_buffer, 1, &viewport);
            vkCmdSetScissorWithCountEXT(ctx.cmd_buffer, 1, &render_area);

            vkCmdSetRasterizerDiscardEnableEXT(ctx.cmd_buffer, VK_FALSE);
            vkCmdSetCullModeEXT(ctx.cmd_buffer, VK_CULL_MODE_BACK_BIT);
            vkCmdSetFrontFaceEXT(ctx.cmd_buffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
            vkCmdSetPrimitiveTopologyEXT(ctx.cmd_buffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
            vkCmdSetPolygonModeEXT(ctx.cmd_buffer, VK_POLYGON_MODE_FILL);
            vkCmdSetPrimitiveRestartEnableEXT(ctx.cmd_buffer, VK_FALSE);
            vkCmdSetVertexInputEXT(ctx.cmd_buffer, 0, nullptr, 0, nullptr);

            // Включаем тест и запись глубины для корректного перекрытия объектов
            vkCmdSetDepthTestEnableEXT(ctx.cmd_buffer, VK_TRUE);
            vkCmdSetDepthWriteEnableEXT(ctx.cmd_buffer, VK_TRUE);
            vkCmdSetDepthCompareOpEXT(ctx.cmd_buffer, VK_COMPARE_OP_LESS_OR_EQUAL);
            vkCmdSetDepthBiasEnableEXT(ctx.cmd_buffer, VK_FALSE);
            vkCmdSetStencilTestEnableEXT(ctx.cmd_buffer, VK_FALSE);

            VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
            vkCmdSetRasterizationSamplesEXT(ctx.cmd_buffer, samples);
            VkSampleMask sample_mask = 0xFFFFFFFF;
            vkCmdSetSampleMaskEXT(ctx.cmd_buffer, samples, &sample_mask);
            vkCmdSetAlphaToCoverageEnableEXT(ctx.cmd_buffer, VK_FALSE);

            constexpr VkBool32 blend_enables[] = {VK_FALSE};
            vkCmdSetColorBlendEnableEXT(ctx.cmd_buffer, 0, 1, blend_enables);
            constexpr VkColorComponentFlags color_write_masks[] = {
                VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT
            };
            vkCmdSetColorWriteMaskEXT(ctx.cmd_buffer, 0, 1, color_write_masks);
            constexpr VkBool32 color_write_enables[] = {VK_TRUE};
            vkCmdSetColorWriteEnableEXT(ctx.cmd_buffer, 1, color_write_enables);

            // 6. Привязка шейдера
            tryengine::graphics::Shader* shader = data.shader.Get();
            VkShaderStageFlagBits stages[] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
            VkShaderEXT shaders[] = {shader->vertex_shader, shader->fragment_shader};
            vkCmdBindShadersEXT(ctx.cmd_buffer, 2, stages, shaders);

            // 7. Батчинг и Multi-Draw Indirect
            size_t i = 0;
            while (i < valid_draws.size()) {
                const auto& first_cmd = valid_draws[i];
                VkBuffer index_buf = first_cmd.mesh->index_buffer;

                eastl::vector<VkDrawIndexedIndirectCommand> indirect_commands;
                size_t batch_start = i;
                size_t batch_end = i;

                while (batch_end < valid_draws.size()) {
                    const auto& cmd = valid_draws[batch_end];
                    if (cmd.mesh->index_buffer != index_buf) {
                        break;
                    }

                    // Заполняем данные объекта прямо в маппнутый буфер
                    PickingObjectGPU& obj = mapped_objects[batch_end];
                    obj.model = cmd.model_matrix;
                    obj.entity_id = cmd.entity_id;
                    obj.position_buffer_addr = cmd.mesh->position_buffer_addr;

                    VkDrawIndexedIndirectCommand indirect{};
                    indirect.indexCount = cmd.mesh->num_indices;
                    indirect.instanceCount = 1;
                    indirect.firstIndex = 0;
                    indirect.vertexOffset = 0;
                    indirect.firstInstance = 0;
                    indirect_commands.push_back(indirect);

                    batch_end++;
                }

                uint32_t draw_count = static_cast<uint32_t>(indirect_commands.size());

                auto indirect_alloc = ctx.AllocateTransient(
                    indirect_commands.data(),
                    sizeof(VkDrawIndexedIndirectCommand) * indirect_commands.size(),
                    alignof(uint32_t)
                );

                vkCmdBindIndexBuffer(ctx.cmd_buffer, index_buf, 0, first_cmd.mesh->index_type);

                VkDeviceAddress batch_objects_bda = objects_alloc.bda_address + (batch_start * sizeof(PickingObjectGPU));

                PushConstants pc{
                    .objects = batch_objects_bda,
                    .camera = camera_alloc.bda_address
                };

                vkCmdPushConstants(
                    ctx.cmd_buffer,
                    shader->layout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,
                    sizeof(PushConstants),
                    &pc
                );

                vkCmdDrawIndexedIndirect(
                    ctx.cmd_buffer,
                    indirect_alloc.buffer,
                    indirect_alloc.offset,
                    draw_count,
                    sizeof(VkDrawIndexedIndirectCommand)
                );

                i = batch_end;
            }

            ctx.EndRendering();

            // 8. Перевод текстуры в layout TRANSFER_SRC и копирование пикселя под мышью
            VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.image = ctx.GetTexture(data.picking_target);
            barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

            VkDependencyInfo dep_info{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dep_info.imageMemoryBarrierCount = 1;
            dep_info.pImageMemoryBarriers = &barrier;
            vkCmdPipelineBarrier2(ctx.cmd_buffer, &dep_info);

            int32_t clamped_x = std::clamp(static_cast<int32_t>(mouse_x), 0, static_cast<int32_t>(width) - 1);
            int32_t clamped_y = std::clamp(static_cast<int32_t>(mouse_y), 0, static_cast<int32_t>(height) - 1);

            VkBufferImageCopy2 copy_region{VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2};
            copy_region.bufferOffset = 0;
            copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy_region.imageSubresource.layerCount = 1;
            copy_region.imageOffset = {clamped_x, clamped_y, 0};
            copy_region.imageExtent = {1, 1, 1};

            VkCopyImageToBufferInfo2 copy_info{VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2};
            copy_info.srcImage = ctx.GetTexture(data.picking_target);
            copy_info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            copy_info.dstBuffer = ctx.GetBuffer(data.readback_buffer);
            copy_info.regionCount = 1;
            copy_info.pRegions = &copy_region;

            vkCmdCopyImageToBuffer2(ctx.cmd_buffer, &copy_info);
        }
    );
}

} // namespace tryeditor