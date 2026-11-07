#include <algorithm>
#include <hlsl++.h>

#include "engine/core/Engine.hpp"
#include "engine/core/HlslppFormatter.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/OpaquePass.hpp"
#include "engine/graphics/RasterState.hpp"
#include "engine/graphics/RenderCommon.hpp"
#include "engine/graphics/ShaderReflection.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryengine::graphics {

namespace {

struct DaslangDrawCall {
    hlslpp::float4x4 world_matrix;
    uint64_t mesh;
    uint64_t material;
    uint64_t entity_id;
};
static_assert(sizeof(DaslangDrawCall) == 96);

struct GeometryPointers {
    VkDeviceAddress positions;   // BDA: Массив позиций (12 байт / вершина)
    VkDeviceAddress attributes;  // BDA: Сырой байтовый буфер упакованных атрибутов
    uint32_t attribute_flags;    // Битовая маска MeshAttributeFlags
    uint32_t attribute_stride;   // Полный размер атрибутов вершины (в байтах)
};

struct alignas(16) GPUObjectData {
    hlslpp::float4x4 model;
    hlslpp::float4x4 normalMatrix;
    GeometryPointers pointers;
};
static_assert(sizeof(GPUObjectData) == 160, "GPUObjectData size must match Standard.slang layout");

struct alignas(16) FragmentDataGPU {
    VkDeviceAddress camera;  // BDA на CameraGPU
    VkDeviceAddress lights;  // BDA на массив PointLightGPU
    uint32_t light_count;
    uint32_t _pad[3];
    hlslpp::float4 ambient_color;
    hlslpp::float4 view_pos;
};

struct PushConstants {
    VkDeviceAddress objects;   // BDA массив объектов GPUObjectData
    VkDeviceAddress fragment;  // BDA указатель на FragmentData (Camera, Lights)
};

eastl::vector<MeshDrawCall> CollectDrawable(core::Engine& engine) {
    eastl::vector<MeshDrawCall> draw_calls;

    auto result = engine.Get<core::ScriptSystem>().SimpleReturnUnsafe<das::Array*>("get_render_obj");
    if (!result.has_value()) {
        LogError(result.error().Message());
        return draw_calls;
    }

    const auto array = *result;
    DaslangDrawCall* draws = reinterpret_cast<DaslangDrawCall*>(array->data);
    eastl::span draw_span(draws, array->size);

    for (const auto& draw_call : draw_span) {
        auto* mesh = reinterpret_cast<Mesh*>(draw_call.mesh);
        auto* material = reinterpret_cast<Material*>(draw_call.material);

        MeshDrawCall cmd{};
        cmd.mesh = mesh;
        cmd.material = material;
        cmd.model_matrix = draw_call.world_matrix;
        cmd.entity_id = draw_call.entity_id;

        const uint16_t shader_id = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(&material->shader) & 0xFFFF);
        const uint16_t material_id = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(material) & 0xFFFF);
        const uint16_t mesh_id = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(mesh) & 0xFFFF);

        cmd.sorting_key = (static_cast<uint64_t>(shader_id) << 32) | (static_cast<uint64_t>(material_id) << 16) |
                          static_cast<uint64_t>(mesh_id);

        draw_calls.push_back(cmd);
    }

    return draw_calls;
}

}  // namespace

void OpaquePass::Collect(CollectContext& ctx) {
    auto camera = ctx.cpu_bb.GetAs<CameraData>(RGTag_v<"Camera">);
    const auto viewport_size = ctx.cpu_bb.GetAs<VkExtent2D>(RGTag_v<"ViewportSize">);
    viewport_ = *viewport_size;

    GlobalLight light;
    light.ambient_color = hlslpp::float4(0.1f, 0.1f, 0.1f, 0);
    light.view_pos = hlslpp::float4(camera->position, 0);

    ctx.cpu_bb.Put(RGTag_v<"GlobalLightUBO">, light);

    point_lights_ = CollectLight(ctx.engine);

    draw_queue_ = CollectDrawable(ctx.engine);
    std::sort(draw_queue_.begin(), draw_queue_.end(),
              [](const MeshDrawCall& a, const MeshDrawCall& b) { return a.sorting_key < b.sorting_key; });
}


void OpaquePass::Setup(RenderGraphBuilder& builder) {
    RGTextureDesc scene_color_desc{
        .width = viewport_.width,
        .height = viewport_.height,
        .layers = 1,
        .mips = 1,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
        .debug_name = "SceneColor"
    };

    RGTextureDesc scene_depth_desc{
        .width = viewport_.width,
        .height = viewport_.height,
        .layers = 1,
        .mips = 1,
        .format = VK_FORMAT_D32_SFLOAT,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .aspect_mask = VK_IMAGE_ASPECT_DEPTH_BIT,
        .debug_name = "SceneDepth"
    };

    RGResourceHandle color_rt = builder.CreateTexture("SceneColor", scene_color_desc);
    RGResourceHandle depth_rt = builder.CreateTexture("SceneDepth", scene_depth_desc);

    color_ = builder.Write(color_rt, RGUsageHint::ColorAttachment);
    depth_ = builder.Write(depth_rt, RGUsageHint::DepthStencilAttachment);

    builder.Export(RGTag_v<"ForwardColor">, color_);
}


void OpaquePass::Execute(RGExecuteContext& ctx) {
    RGColorAttachment color_attachment{};
    color_attachment.handle = color_;
    color_attachment.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.store_op = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.clear_value.color = {{0.1f, 0.1f, 0.12f, 1.0f}};

    RGDepthStencilAttachment depth_attachment{};
    depth_attachment.handle = depth_;
    depth_attachment.depth_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth_attachment.depth_store_op = VK_ATTACHMENT_STORE_OP_STORE;
    depth_attachment.clear_value.depthStencil = {1.0f, 0};

    const VkRect2D render_area{{0, 0}, viewport_};
    ctx.BeginRendering({&color_attachment, 1}, &depth_attachment, render_area);

    SetViewportScissor(ctx.cmd_buffer, viewport_);
    ApplyRasterState(ctx.cmd_buffer);

    RecordDraws(ctx);

    ctx.EndRendering();
}

void OpaquePass::RecordDraws(RGExecuteContext& ctx) {
    // if (draw_queue_.empty()) {
        // return;
    // }

    const auto* camera_cpu = ctx.cpu_bb->GetAs<CameraData>(RGTag_v<"Camera">);
    const auto* light_ubo = ctx.cpu_bb->GetAs<GlobalLight>(RGTag_v<"GlobalLightUBO">);
    TRY_ASSERT(camera_cpu && light_ubo, "Camera or LightUBO missing in CPUBlackboard!");

    CameraGPU camera_gpu{
        .view = camera_cpu->view,
        .proj = camera_cpu->proj
    };
    TransientAllocation camera_alloc = ctx.AllocateTransient(&camera_gpu, sizeof(CameraGPU), alignof(CameraGPU));

    TransientAllocation lights_alloc = ctx.AllocateTransient(
        point_lights_.data(), sizeof(PointLightGPU) * point_lights_.size(), alignof(PointLightGPU));

    FragmentDataGPU frag_data{
        .camera = camera_alloc.bda_address,
        .lights = lights_alloc.bda_address,
        .light_count = static_cast<uint32_t>(point_lights_.size()),
        .ambient_color = light_ubo->ambient_color,
        .view_pos = light_ubo->view_pos
    };
    TransientAllocation frag_alloc = ctx.AllocateTransient(&frag_data, sizeof(FragmentDataGPU), alignof(FragmentDataGPU));

    const auto& draw_queue = draw_queue_;

    TransientAllocation objects_alloc = ctx.AllocateTransient(
        nullptr, sizeof(GPUObjectData) * draw_queue.size(), alignof(GPUObjectData));
    GPUObjectData* mapped_objects = reinterpret_cast<GPUObjectData*>(objects_alloc.mapped_ptr);

    Shader* current_shader = nullptr;
    const Material* current_material = nullptr;

    size_t i = 0;
    while (i < draw_queue.size()) {
        const auto& first_cmd = draw_queue[i];
        if (!first_cmd.mesh->position_buffer_addr || !first_cmd.mesh->attribute_buffer_addr ||
            !first_cmd.mesh->index_buffer || !first_cmd.material) {
            i++;
            continue;
        }

        Shader& shader = *first_cmd.material->shader;
        const Material& material = *first_cmd.material;
        const Mesh& mesh = *first_cmd.mesh;
        VkBuffer index_buf = mesh.index_buffer;

        // Привязка шейдера
        if (&shader != current_shader) {
            VkShaderStageFlagBits stages[] = {VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT};
            VkShaderEXT shaders[] = {shader.vertex_shader, shader.fragment_shader};
            vkCmdBindShadersEXT(ctx.cmd_buffer, 2, stages, shaders);

            current_shader = &shader;
            current_material = nullptr;
        }

        // Привязка материала
        if (&material != current_material) {
            if (shader.reflection.HasMaterialBindings() && material.descriptor_set != VK_NULL_HANDLE) {
                vkCmdBindDescriptorSets(ctx.cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader.layout,
                                        static_cast<uint32_t>(BindingScope::Material), 1, &material.descriptor_set, 0,
                                        nullptr);
            }
            current_material = &material;
        }

        // Формирование батча
        eastl::vector<VkDrawIndexedIndirectCommand> indirect_commands;
        size_t batch_start = i;
        size_t batch_end = i;

        while (batch_end < draw_queue.size()) {
            const auto& cmd = draw_queue[batch_end];
            if (cmd.material->shader.Get() != current_shader || cmd.material != current_material ||
                cmd.mesh->index_buffer != index_buf) {
                break;
            }

            // Записываем данные объекта прямо в маппленный транзиентный буфер!
            GPUObjectData& obj = mapped_objects[batch_end];
            obj.model = cmd.model_matrix;
            obj.normalMatrix = hlslpp::transpose(hlslpp::inverse(cmd.model_matrix));
            obj.pointers = {mesh.position_buffer_addr, mesh.attribute_buffer_addr, (uint32_t)mesh.attribute_flags,
                            mesh.attribute_stride};

            VkDrawIndexedIndirectCommand indirect{};
            indirect.indexCount = mesh.num_indices;
            indirect.instanceCount = 1;
            indirect.firstIndex = 0;
            indirect.vertexOffset = 0;
            indirect.firstInstance = 0;
            indirect_commands.push_back(indirect);

            batch_end++;
        }

        uint32_t draw_count = static_cast<uint32_t>(indirect_commands.size());

        TransientAllocation indirect_alloc =
            ctx.AllocateTransient(indirect_commands.data(),
                                  sizeof(VkDrawIndexedIndirectCommand) * indirect_commands.size(), sizeof(uint32_t));

        vkCmdBindIndexBuffer(ctx.cmd_buffer, index_buf, 0, first_cmd.mesh->index_type);

        PushConstants pc{};
        pc.objects = objects_alloc.bda_address + (batch_start * sizeof(GPUObjectData));
        pc.fragment = frag_alloc.bda_address;

        vkCmdPushConstants(ctx.cmd_buffer, shader.layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(PushConstants), &pc);

        vkCmdDrawIndexedIndirect(ctx.cmd_buffer, indirect_alloc.buffer, indirect_alloc.offset, draw_count,
                                 sizeof(VkDrawIndexedIndirectCommand));

        i = batch_end;
    }
}

}  // namespace tryengine::graphics