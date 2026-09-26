#include "engine/graphics/OpaqueGeometryPass.hpp"

#include <EASTL/span.h>
#include <algorithm>
#include <hlsl++.h>

#include "engine/core/Engine.hpp"
#include "engine/core/HlslppFormatter.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/MeshDrawCall.hpp"
#include "engine/graphics/PipelineDescriptor.hpp"
#include "engine/graphics/PipelineManager.hpp"
#include "engine/graphics/RenderCommon.hpp"
#include "engine/graphics/ShaderReflection.hpp"
#include "engine/graphics/loaders/LightPass.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"
#include "engine/resources/Vertex.hpp"

namespace tryengine::graphics {
struct DaslangDrawCall {
    hlslpp::float4x4 world_matrix;
    uint64_t mesh;
    uint64_t material;
};
static_assert(sizeof(DaslangDrawCall) == 80);

eastl::vector<MeshDrawCall> OpaqueGeometryPass::CollectDrawable(core::Engine& engine) {
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

        const uint16_t shader_id = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(&material->shader) & 0xFFFF);
        const uint16_t material_id = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(material) & 0xFFFF);
        const uint16_t mesh_id = static_cast<uint16_t>(reinterpret_cast<uintptr_t>(mesh) & 0xFFFF);

        cmd.sorting_key = (static_cast<uint64_t>(shader_id) << 32) | (static_cast<uint64_t>(material_id) << 16) |
                          static_cast<uint64_t>(mesh_id);

        draw_calls.push_back(cmd);
    }

    return draw_calls;
}

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

// Структуры под Slang-лейаут (Standard.slang)
struct alignas(16) CameraGPU {
    hlslpp::float4x4 view;
    hlslpp::float4x4 proj;
};

struct alignas(16) FragmentDataGPU {
    VkDeviceAddress camera;       // BDA на CameraGPU
    VkDeviceAddress lights;       // BDA на массив PointLightGPU
    uint32_t light_count;
    uint32_t _pad[3];
    hlslpp::float4 ambient_color;
    hlslpp::float4 view_pos;
};


struct PushConstants {
    VkDeviceAddress objects;  // BDA массив объектов GPUObjectData
    VkDeviceAddress fragment; // BDA указатель на FragmentData (Camera, Lights)
};

static_assert(sizeof(GPUObjectData) == 160, "GPUObjectData size must match Standard.slang layout");

void OpaqueGeometryPass::ExecuteDrawCommands(const RGExecuteContext& ctx, eastl::span<const MeshDrawCall> queue) {
    if (queue.empty()) {
        return;
    }

    // =========================================================================
    // 1. ПОДГОТОВКА ГЛОБАЛЬНЫХ ДАННЫХ ПАССА (ОДИН РАЗ НА ПАСС)
    // =========================================================================
    const auto* camera_cpu = ctx.cpu_bb->GetAs<CameraData>(RGTag_v<"Camera">);
    const auto* light_ubo = ctx.cpu_bb->GetAs<GlobalLight>(RGTag_v<"GlobalLightUBO">);
    const uint32_t* light_count = ctx.cpu_bb->GetAs<uint32_t>(RGTag_v<"LightCount">);
    TRY_ASSERT(camera_cpu && light_ubo, "Camera or LightUBO missing in CPUBlackboard!");

    // Выделяем память под Камеру
    CameraGPU camera_gpu{
        .view = camera_cpu->view,
        .proj = camera_cpu->proj
    };
    TransientAllocation camera_alloc = ctx.AllocateTransient(&camera_gpu, sizeof(CameraGPU), alignof(CameraGPU));

    // Получаем BDA буфера источников света из RenderGraph / Blackboard
    RGResourceHandle light_buf_handle = ctx.frame_bb->Get(RGTag_v<"PointLightBuffer">);
    VkBuffer light_buffer = ctx.GetBuffer(light_buf_handle);

    VkBufferDeviceAddressInfo light_bda_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
    light_bda_info.buffer = light_buffer;
    VkDeviceAddress light_buffer_bda = light_buffer ? vkGetBufferDeviceAddress(ctx.device, &light_bda_info) : 0;

    // Формируем и загружаем FragmentData
    FragmentDataGPU frag_data{
        .camera = camera_alloc.bda_address,
        .lights = light_buffer_bda,
        .light_count = *light_count,
        .ambient_color = light_ubo->ambient_color,
        .view_pos = light_ubo->view_pos
    };
    TransientAllocation frag_alloc = ctx.AllocateTransient(&frag_data, sizeof(FragmentDataGPU), alignof(FragmentDataGPU));

    // =========================================================================
    // 2. СОРТИРОВКА И ПРЕДВАРИТЕЛЬНОЕ ВЫДЕЛЕНИЕ ПАМЯТИ ПОД ОБЪЕКТЫ
    // =========================================================================
    eastl::vector<MeshDrawCall> draw_queue(queue.begin(), queue.end());
    std::sort(draw_queue.begin(), draw_queue.end(),
              [](const MeshDrawCall& a, const MeshDrawCall& b) { return a.sorting_key < b.sorting_key; });

    // Выделяем транзиентную память СРАЗУ ПОД ВСЕ ОБЪЕКТЫ кадра
    TransientAllocation objects_alloc = ctx.AllocateTransient(
        nullptr, sizeof(GPUObjectData) * draw_queue.size(), alignof(GPUObjectData));
    GPUObjectData* mapped_objects = reinterpret_cast<GPUObjectData*>(objects_alloc.mapped_ptr);

    // --- 1. Полный набор динамических состояний для Shader Objects ---
    vkCmdSetRasterizerDiscardEnableEXT(ctx.cmd_buffer, VK_FALSE);
    vkCmdSetCullModeEXT(ctx.cmd_buffer, VK_CULL_MODE_BACK_BIT);
    vkCmdSetFrontFaceEXT(ctx.cmd_buffer, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    vkCmdSetPrimitiveTopologyEXT(ctx.cmd_buffer, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    vkCmdSetPolygonModeEXT(ctx.cmd_buffer, VK_POLYGON_MODE_FILL);
    vkCmdSetPrimitiveRestartEnableEXT(ctx.cmd_buffer, VK_FALSE);

    // BDA без стандартных Vertex Buffers
    vkCmdSetVertexInputEXT(ctx.cmd_buffer, 0, nullptr, 0, nullptr);

    // Depth & Stencil
    vkCmdSetDepthTestEnableEXT(ctx.cmd_buffer, VK_TRUE);
    vkCmdSetDepthWriteEnableEXT(ctx.cmd_buffer, VK_TRUE);
    vkCmdSetDepthCompareOpEXT(ctx.cmd_buffer, VK_COMPARE_OP_LESS);
    vkCmdSetDepthBiasEnableEXT(ctx.cmd_buffer, VK_FALSE);
    vkCmdSetStencilTestEnableEXT(ctx.cmd_buffer, VK_FALSE);

    // Multisampling
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    vkCmdSetRasterizationSamplesEXT(ctx.cmd_buffer, samples);
    VkSampleMask sample_mask = 0xFFFFFFFF;
    vkCmdSetSampleMaskEXT(ctx.cmd_buffer, samples, &sample_mask);
    vkCmdSetAlphaToCoverageEnableEXT(ctx.cmd_buffer, VK_FALSE);

    // Color Blending & Write Masks
    constexpr VkBool32 blend_enables[] = {VK_FALSE};
    vkCmdSetColorBlendEnableEXT(ctx.cmd_buffer, 0, 1, blend_enables);

    constexpr VkColorComponentFlags color_write_masks[] = {
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    vkCmdSetColorWriteMaskEXT(ctx.cmd_buffer, 0, 1, color_write_masks);

    constexpr VkColorBlendEquationEXT blend_equations[] = {
        {
            .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstColorBlendFactor = VK_BLEND_FACTOR_ZERO,
            .colorBlendOp = VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
            .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
            .alphaBlendOp = VK_BLEND_OP_ADD,
        }
    };
    vkCmdSetColorBlendEquationEXT(ctx.cmd_buffer, 0, 1, blend_equations);

    constexpr VkBool32 color_write_enables[] = {VK_TRUE};
    vkCmdSetColorWriteEnableEXT(ctx.cmd_buffer, 1, color_write_enables);

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
            // obj.normalMatrix = hlslpp::inverse(cmd.model_matrix);
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

        // Загружаем только Indirect Команды для текущего батча
        TransientAllocation indirect_alloc =
            ctx.AllocateTransient(indirect_commands.data(),
                                  sizeof(VkDrawIndexedIndirectCommand) * indirect_commands.size(), sizeof(uint32_t));

        vkCmdBindIndexBuffer(ctx.cmd_buffer, index_buf, 0, first_cmd.mesh->index_type);

        // Расчёт BDA адреса массива объектов конкретно для данного батча
        VkDeviceAddress batch_objects_bda = objects_alloc.bda_address + (batch_start * sizeof(GPUObjectData));

        PushConstants pc{};
        pc.objects = batch_objects_bda;      // BDA смещённого массива объектов
        pc.fragment = frag_alloc.bda_address; // BDA общего FragmentData для пасса

        vkCmdPushConstants(
            ctx.cmd_buffer,
            shader.layout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0,
            sizeof(PushConstants),
            &pc
        );

        vkCmdDrawIndexedIndirect(ctx.cmd_buffer, indirect_alloc.buffer, indirect_alloc.offset, draw_count,
                                 sizeof(VkDrawIndexedIndirectCommand));

        i = batch_end;
    }
}
}