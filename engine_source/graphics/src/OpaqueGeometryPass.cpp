#include "engine/graphics/OpaqueGeometryPass.hpp"

#include <EASTL/span.h>
#include <algorithm>
#include <hlsl++.h>

#include "engine/core/Assert.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/HlslppFormatter.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/MeshDrawCall.hpp"
#include "engine/graphics/PipelineDescriptor.hpp"
#include "engine/graphics/PipelineManager.hpp"
#include "engine/graphics/RenderCommon.hpp"
#include "engine/graphics/ShaderReflection.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryengine::graphics {

struct DaslangDrawCall {
    hlslpp::float4x4 world_matrix;
    uint64_t mesh;
    uint64_t material;
};
static_assert(sizeof(DaslangDrawCall) == 80);

inline uint64_t MakeSortingKey(uint8_t pass_layer, uint16_t pipeline_id, uint16_t material_id, uint16_t mesh_id,
                               uint16_t depth = 0) {
    return (static_cast<uint64_t>(pass_layer) << 62) | (static_cast<uint64_t>(pipeline_id) << 48) |
           (static_cast<uint64_t>(material_id) << 32) | (static_cast<uint64_t>(mesh_id) << 16) |
           (static_cast<uint64_t>(depth));
}

eastl::vector<MeshDrawCall> OpaqueGeometryPass::CollectDrawable(core::Engine& engine,
                                                                PipelineManager& pipeline_manager) {
    eastl::vector<MeshDrawCall> draw_calls;

    auto result = engine.Get<core::ScriptSystem>().SimpleReturnUnsafe<das::Array*>("get_render_obj");

    if (!result.has_value())
        LogError(result.error().Message());

    const auto array = *result;
    DaslangDrawCall* draws = reinterpret_cast<DaslangDrawCall*>(array->data);
    eastl::span draw_span(draws, array->size);
    // if (array->size > 0)
    //     LogInfo("drawcall's size = {}", array->size);

    for (const auto& draw_call : draw_span) {
        Mesh* mesh = reinterpret_cast<Mesh*>(draw_call.mesh);
        Material* material = reinterpret_cast<Material*>(draw_call.material);

        PipelineDescriptor desc;
        desc.fragment_shader = material->shader().fragment_shader;
        desc.vertex_shader = material->shader().vertex_shader;
        desc.vertex_format = mesh->v_format;
        desc.cull_mode = SDL_GPU_CULLMODE_BACK;
        auto* pipeline = pipeline_manager.GetOrCreatePipeline(desc);

        if (!pipeline)
            continue;

        uint16_t pipeline_id = desc.GetHashCode() & 0xFFFF;
        uint16_t material_id = reinterpret_cast<uintptr_t>(material) & 0xFFFF;
        uint16_t mesh_id = reinterpret_cast<uintptr_t>(mesh) & 0xFFFF;

        MeshDrawCall cmd;
        cmd.sorting_key = MakeSortingKey(0, pipeline_id, material_id, mesh_id);

        cmd.vertex_buffer = mesh->vertex_buffer;
        cmd.index_buffer = mesh->index_buffer;
        cmd.num_indices = mesh->num_indices;
        cmd.index_format = mesh->i_format;
        cmd.pipeline = pipeline;
        cmd.material = material;
        cmd.model_matrix = draw_call.world_matrix;

        draw_calls.push_back(cmd);
    }

    return draw_calls;
}

__forceinline constexpr SDL_GPUIndexElementSize IndexFormat(const resources::IndexFormat format) {
    return format == resources::IndexFormat::UInt32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT;
}

namespace {

void BindPassResources(const RGExecuteContext& ctx, const Shader& shader) {
    TRY_CHECK(ctx.frame_bb != nullptr, "Blackboard is null in Render Pass!");

    for (const auto& binding : shader.reflection.bindings) {
        const bool is_storage_buffer = binding.kind == ShaderResourceKind::StorageBufferRead ||
                                       binding.kind == ShaderResourceKind::StorageBufferWrite;

        if (!binding.IsPass() && !is_storage_buffer) {
            continue;
        }

        switch (binding.kind) {
            case ShaderResourceKind::StorageBufferRead:
            case ShaderResourceKind::StorageBufferWrite: {
                RGResourceHandle handle = ctx.frame_bb->Get(binding.name_hash);
                TRY_CHECK(handle.IsValid(), "Pass resource '{}' missing in Blackboard!", binding.name.c_str());

                SDL_GPUBuffer* buffer = ctx.GetBuffer(handle);
                TRY_CHECK(buffer != nullptr, "Buffer '{}' is null in Blackboard!", binding.name.c_str());

                uint32_t textures_before = 0;
                for (const auto& other : shader.reflection.bindings) {
                    if (other.stage != binding.stage)
                        continue;
                    if (other.kind == ShaderResourceKind::SampledTexture && other.binding < binding.binding) {
                        ++textures_before;
                    }
                }
                uint32_t storage_buffer_slot = binding.binding - textures_before;

                if (binding.stage == ShaderStage::Vertex) {
                    SDL_BindGPUVertexStorageBuffers(ctx.gpu_pass, storage_buffer_slot, &buffer, 1);
                } else {
                    SDL_BindGPUFragmentStorageBuffers(ctx.gpu_pass, storage_buffer_slot, &buffer, 1);
                }
                break;
            }
            case ShaderResourceKind::UniformBuffer: {
                const auto& data = ctx.cpu_bb->GetSpan(binding.name_hash);
                if (!data.has_value()) {
                    LogError("binding name = {} Not found in CPU BB", binding.name);
                }

                if (binding.stage == ShaderStage::Vertex) {
                    SDL_PushGPUVertexUniformData(ctx.cmd_buffer, binding.binding, data->data(), static_cast<uint32_t>(data->size_bytes()));
                } else {
                    SDL_PushGPUFragmentUniformData(ctx.cmd_buffer, binding.binding, data->data(), static_cast<uint32_t>(data->size_bytes()));
                }
                break;
            }
            case ShaderResourceKind::SampledTexture: {
                RGResourceHandle handle = ctx.frame_bb->Get(binding.name_hash);
                TRY_CHECK(handle.IsValid(), "Pass resource '{}' missing in Blackboard!", binding.name.c_str());

                SDL_GPUTexture* texture = ctx.GetTexture(handle);
                TRY_CHECK(texture != nullptr, "Texture '{}' is null in Blackboard!", binding.name.c_str());
                break;
            }
            default:
                TRY_CHECK(false, "Unsupported Pass resource kind for '{}'", binding.name.c_str());
        }
    }
}

void BindMaterialResources(const RGExecuteContext& ctx, const Material& mat) {
    for (const auto& ubo : mat.cached_ubo_bindings) {
        const void* ubo_data_ptr = mat.uniform_buffer.data() + ubo.offset_in_buffer;
        if (ubo.stage == ShaderStage::Vertex) {
            SDL_PushGPUVertexUniformData(ctx.cmd_buffer, ubo.slot, ubo_data_ptr, ubo.size);
        } else {
            SDL_PushGPUFragmentUniformData(ctx.cmd_buffer, ubo.slot, ubo_data_ptr, ubo.size);
        }
    }

    for (const auto& tex : mat.textures) {
        TRY_CHECK(tex.texture && tex.texture->sampler && tex.texture->texture,
                  "Material bound with invalid or null texture/sampler!");
        SDL_GPUTextureSamplerBinding tsb{tex.texture->texture, tex.texture->sampler};
        SDL_BindGPUFragmentSamplers(ctx.gpu_pass, tex.slot, &tsb, 1);
    }
}

void BindPerObjResources(const RGExecuteContext& ctx, const MeshDrawCall& command) {
    const Shader& shader = command.material->shader();

    for (const auto& binding : shader.reflection.bindings) {
        if (!binding.IsPerObj() || binding.kind != ShaderResourceKind::UniformBuffer) {
            continue;
        }

        struct alignas(16) StandardPerObjUBO {
            hlslpp::float4x4 model;
            hlslpp::float4x4 normalMatrix;
        } frame_ubo{};

        frame_ubo.model = command.model_matrix;
        frame_ubo.normalMatrix = hlslpp::transpose(hlslpp::inverse(frame_ubo.model));

        if (binding.stage == ShaderStage::Vertex) {
            SDL_PushGPUVertexUniformData(ctx.cmd_buffer, binding.binding, &frame_ubo, sizeof(StandardPerObjUBO));
        } else {
            SDL_PushGPUFragmentUniformData(ctx.cmd_buffer, binding.binding, &frame_ubo, sizeof(StandardPerObjUBO));
        }
    }
}

}  // namespace

void OpaqueGeometryPass::ExecuteDrawCommands(const RGExecuteContext& ctx,
                                             eastl::span<const MeshDrawCall> queue) {
    if (queue.empty() || !ctx.gpu_pass)
        return;

    eastl::vector<MeshDrawCall> draw_queue(queue.begin(), queue.end());
    std::sort(draw_queue.begin(), draw_queue.end(),
              [](const MeshDrawCall& a, const MeshDrawCall& b) { return a.sorting_key < b.sorting_key; });

    SDL_GPUGraphicsPipeline* current_pipeline = nullptr;
    Material* current_material = nullptr;
    SDL_GPUBuffer* current_vertex_buffer = nullptr;
    SDL_GPUBuffer* current_index_buffer = nullptr;
    Shader* current_shader = nullptr;

    for (const auto& command : draw_queue) {
        if (!command.pipeline || !command.vertex_buffer || !command.material)
            continue;

        Shader& shader = command.material->shader();

        // 1. Смена пайплайна
        if (command.pipeline != current_pipeline) {
            SDL_BindGPUGraphicsPipeline(ctx.gpu_pass, command.pipeline);
            current_pipeline = command.pipeline;
            current_material = nullptr;
            current_vertex_buffer = nullptr;
            current_index_buffer = nullptr;
            current_shader = nullptr;
        }

        // 2. Ресурсы уровня Pass (Только при смене шейдера, строго из Blackboard)
        if (&shader != current_shader) {
            // LogInfo("[ExecuteDrawCommands] смена шейдера -> вызываем BindPassResources");
            BindPassResources(ctx, shader);
            current_shader = &shader;
        }

        // 3. Ресурсы уровня Material (При смене материала)
        if (command.material != current_material) {
            // LogInfo("[ExecuteDrawCommands] смена материала -> вызываем BindMaterialResources");
            BindMaterialResources(ctx, *command.material);
            current_material = command.material;
        }

        // 4. Ресурсы уровня PerDraw (Каждый draw call)
        BindPerObjResources(ctx, command);

        // 5. Геометрия и Draw Call
        if (command.vertex_buffer != current_vertex_buffer) {
            SDL_GPUBufferBinding vb = {command.vertex_buffer, 0};
            SDL_BindGPUVertexBuffers(ctx.gpu_pass, 0, &vb, 1);
            current_vertex_buffer = command.vertex_buffer;
        }

        if (command.index_buffer != current_index_buffer) {
            SDL_GPUBufferBinding ib = {command.index_buffer, 0};
            SDL_BindGPUIndexBuffer(ctx.gpu_pass, &ib, IndexFormat(command.index_format));
            current_index_buffer = command.index_buffer;
        }

        SDL_DrawGPUIndexedPrimitives(ctx.gpu_pass, command.num_indices, 1, 0, 0, 0);
    }
}

}  // namespace tryengine::graphics