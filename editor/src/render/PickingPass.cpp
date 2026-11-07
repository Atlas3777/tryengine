#include "editor/render/PickingPass.hpp"

#include <algorithm>
#include <cstring>

#include "editor/render/EditorRender.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/MeshDrawCall.hpp"
#include "engine/graphics/OpaquePass.hpp"
#include "engine/graphics/RasterState.hpp"
#include "engine/graphics/RenderCommon.hpp"
#include "engine/graphics/VulkanDevice.hpp"

namespace tryeditor {

namespace {

struct alignas(16) PickingObjectGPU {
    hlslpp::float4x4 model;
    uint64_t entity_id;
    VkDeviceAddress position_buffer_addr;
};

struct PickingPushConstants {
    VkDeviceAddress objects;  // BDA указатель на PickingObjectGPU[]
    VkDeviceAddress camera;   // BDA указатель на CameraGPU
};

}  // namespace

PickingPass::PickingPass(tryengine::graphics::VulkanDevice& device,
                         tryengine::resources::ResourceHandle<tryengine::graphics::Shader> shader)
    : device_(device), shader_(shader) {
    for (auto& rb : readback_) {
        VkBufferCreateInfo buf_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        buf_info.size = sizeof(uint64_t);  // entity_id
        buf_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo alloc_info{};
        alloc_info.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
        alloc_info.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo result{};
        VkResult res = vmaCreateBuffer(device_.GetAllocator(), &buf_info, &alloc_info, &rb.buffer, &rb.allocation, &result);
        TRY_CHECK(res == VK_SUCCESS, "Не удалось создать PickingReadbackBuffer");
        rb.mapped_ptr = result.pMappedData;
    }
}

PickingPass::~PickingPass() {
    for (auto& rb : readback_) {
        if (rb.buffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(device_.GetAllocator(), rb.buffer, rb.allocation);
        }
    }
}

void PickingPass::Collect(tryengine::graphics::CollectContext& ctx) {
    frame_index_ = ctx.frame_index;
    opaque_ = ctx.graph.Get<tryengine::graphics::OpaquePass>(EditorRender::kEditorScope);
    requested_ = false;

    // 1. Всегда импортируем readback-буфер текущего кадра в граф
    Readback& rb = readback_[frame_index_];
    tryengine::graphics::RGBufferDesc desc{
        .size = sizeof(uint64_t),
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .memory_usage = VMA_MEMORY_USAGE_GPU_TO_CPU,
        .debug_name = "PickingReadbackBuffer"
    };
    ctx.graph.ImportExternalBuffer(EditorRender::kEditorScope,tryengine::graphics::RGTag_v<"PickingReadback">, rb.buffer, desc);

    // 2. Вычитываем результат прошлого запроса
    if (rb.pending && rb.mapped_ptr) {
        uint64_t picked_entity_id = 0;
        std::memcpy(&picked_entity_id, rb.mapped_ptr, sizeof(uint64_t));
        rb.pending = false;

        if (picked_entity_id != 0) {
            LogInfo("Выбран объект с Entity ID: {}", picked_entity_id);
            auto& script = ctx.engine.Get<tryengine::core::ScriptSystem>();
            script.InvokeFunctionFast("SetSelectedEntity", picked_entity_id);
        }
    }

    // 3. Проверяем наличие нового клика
    if (!shader_ || !opaque_) return;

    auto& script = ctx.engine.Get<tryengine::core::ScriptSystem>();
    auto size = script.SimpleReturnUnsafe<das::float2>("GetEditorViewportSize");
    if (!size.has_value()) return;

    auto mouse = script.SimpleReturnUnsafe<Mouse*>("GetEditorSceneViewportMouse", *size);
    if (!mouse.has_value()) return;

    const Mouse* m = *mouse;
    if (!m->pressed) return;

    requested_ = true;
    mouse_x_ = static_cast<uint32_t>(std::max(0.0f, m->pos.x));
    mouse_y_ = static_cast<uint32_t>(std::max(0.0f, m->pos.y));
}

void PickingPass::Setup(tryengine::graphics::RenderGraphBuilder& builder) {
    const auto* extent = builder.GetCPUBlackboard().GetAs<VkExtent2D>(tryengine::graphics::RGTag_v<"ViewportSize">);
    if (!extent || extent->width == 0 || extent->height == 0) return;

    tryengine::graphics::RGTextureDesc picking_desc{
        .width = extent->width,
        .height = extent->height,
        .layers = 1,
        .mips = 1,
        .format = VK_FORMAT_R32G32_UINT,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
        .debug_name = "PickingTexture"
    };

    picking_ = builder.CreateTexture("PickingTexture", picking_desc);
    picking_ = builder.Write(picking_, tryengine::graphics::RGUsageHint::ColorAttachment);
    depth_ = builder.Write(tryengine::graphics::RGTag_v<"SceneDepth">,
                           tryengine::graphics::RGUsageHint::DepthStencilAttachment);
    readback_handle_ = builder.Write(tryengine::graphics::RGTag_v<"PickingReadback">, tryengine::graphics::RGUsageHint::TransferDst);

    builder.MarkSideEffect();
}

void PickingPass::Execute(tryengine::graphics::RGExecuteContext& ctx) {
    if (!requested_ || !opaque_) return;

    const auto* extent = ctx.cpu_bb->GetAs<VkExtent2D>(tryengine::graphics::RGTag_v<"ViewportSize">);
    const auto* camera_cpu = ctx.cpu_bb->GetAs<tryengine::graphics::CameraData>(tryengine::graphics::RGTag_v<"Camera">);
    TRY_ASSERT(extent && camera_cpu, "ViewportSize/Camera missing in CPUBlackboard!");
    const uint32_t width = extent->width;
    const uint32_t height = extent->height;

    // 1. Отбор валидных мешей из OpaquePass
    eastl::vector<tryengine::graphics::MeshDrawCall> valid_draws;
    for (const auto& draw : opaque_->DrawQueue()) {
        if (draw.mesh && draw.mesh->position_buffer_addr && draw.mesh->index_buffer) {
            valid_draws.push_back(draw);
        }
    }
    if (valid_draws.empty()) return;

    // 2. Выделение BDA-памяти под камеру и объекты
    tryengine::graphics::CameraGPU camera_gpu{
        .view = camera_cpu->view,
        .proj = camera_cpu->proj
    };
    auto camera_alloc = ctx.AllocateTransient(&camera_gpu, sizeof(tryengine::graphics::CameraGPU), alignof(tryengine::graphics::CameraGPU));

    auto objects_alloc = ctx.AllocateTransient(nullptr, sizeof(PickingObjectGPU) * valid_draws.size(), alignof(PickingObjectGPU));
    PickingObjectGPU* mapped_objects = reinterpret_cast<PickingObjectGPU*>(objects_alloc.mapped_ptr);

    // 3. Attachments
    tryengine::graphics::RGColorAttachment color_att{};
    color_att.handle = picking_;
    color_att.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_att.store_op = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.clear_value.color.uint32[0] = 0;
    color_att.clear_value.color.uint32[1] = 0;

    tryengine::graphics::RGDepthStencilAttachment depth_att{};
    depth_att.handle = depth_;
    depth_att.depth_load_op = VK_ATTACHMENT_LOAD_OP_LOAD;
    depth_att.depth_store_op = VK_ATTACHMENT_STORE_OP_STORE;

    const VkRect2D render_area{{0, 0}, *extent};
    ctx.BeginRendering(eastl::span(&color_att, 1), &depth_att, render_area);

    tryengine::graphics::SetViewportScissor(ctx.cmd_buffer, *extent);

    // Включаем динамические состояния растеризации и теста глубины
    vkCmdSetRasterizerDiscardEnableEXT(ctx.cmd_buffer, VK_FALSE);
    vkCmdSetCullModeEXT(ctx.cmd_buffer, VK_CULL_MODE_BACK_BIT);
    vkCmdSetFrontFaceEXT(ctx.cmd_buffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetPrimitiveTopologyEXT(ctx.cmd_buffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetPolygonModeEXT(ctx.cmd_buffer, VK_POLYGON_MODE_FILL);
    vkCmdSetPrimitiveRestartEnableEXT(ctx.cmd_buffer, VK_FALSE);
    vkCmdSetVertexInputEXT(ctx.cmd_buffer, 0, nullptr, 0, nullptr);

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

    tryengine::graphics::Shader* shader = shader_.Get();
    VkShaderStageFlagBits stages[] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
    VkShaderEXT shaders[] = {shader->vertex_shader, shader->fragment_shader};
    vkCmdBindShadersEXT(ctx.cmd_buffer, 2, stages, shaders);

    // 4. Отрисовка Multi-Draw Indirect батчами
    size_t i = 0;
    while (i < valid_draws.size()) {
        const auto& first_cmd = valid_draws[i];
        VkBuffer index_buf = first_cmd.mesh->index_buffer;

        eastl::vector<VkDrawIndexedIndirectCommand> indirect_commands;
        size_t batch_start = i;
        size_t batch_end = i;

        while (batch_end < valid_draws.size()) {
            const auto& cmd = valid_draws[batch_end];
            if (cmd.mesh->index_buffer != index_buf) break;

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
            indirect_commands.data(), sizeof(VkDrawIndexedIndirectCommand) * indirect_commands.size(), alignof(uint32_t));

        vkCmdBindIndexBuffer(ctx.cmd_buffer, index_buf, 0, first_cmd.mesh->index_type);

        PickingPushConstants pc{
            .objects = objects_alloc.bda_address + (batch_start * sizeof(PickingObjectGPU)),
            .camera = camera_alloc.bda_address
        };
        vkCmdPushConstants(ctx.cmd_buffer, shader->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(PickingPushConstants), &pc);

        vkCmdDrawIndexedIndirect(ctx.cmd_buffer, indirect_alloc.buffer, indirect_alloc.offset, draw_count,
                                 sizeof(VkDrawIndexedIndirectCommand));

        i = batch_end;
    }

    ctx.EndRendering();

    // 5. Копирование пикселя под мышью в readback буфер
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.image = ctx.GetTexture(picking_);
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dep_info{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep_info.imageMemoryBarrierCount = 1;
    dep_info.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(ctx.cmd_buffer, &dep_info);

    const int32_t x = std::clamp(static_cast<int32_t>(mouse_x_), 0, static_cast<int32_t>(width) - 1);
    const int32_t y = std::clamp(static_cast<int32_t>(mouse_y_), 0, static_cast<int32_t>(height) - 1);

    VkBufferImageCopy2 copy_region{VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2};
    copy_region.bufferOffset = 0;
    copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy_region.imageSubresource.layerCount = 1;
    copy_region.imageOffset = {x, y, 0};
    copy_region.imageExtent = {1, 1, 1};

    VkCopyImageToBufferInfo2 copy_info{VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2};
    copy_info.srcImage = ctx.GetTexture(picking_);
    copy_info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    copy_info.dstBuffer = ctx.GetBuffer(readback_handle_);
    copy_info.regionCount = 1;
    copy_info.pRegions = &copy_region;
    vkCmdCopyImageToBuffer2(ctx.cmd_buffer, &copy_info);

    readback_[frame_index_].pending = true;
}

}  // namespace tryeditor