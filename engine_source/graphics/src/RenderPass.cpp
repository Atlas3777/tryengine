#include "engine/graphics/RenderPass.hpp"

#include <algorithm>
#include <hlsl++.h>

#include "engine/core/Assert.hpp"
#include "engine/graphics/ShaderReflection.hpp"

namespace tryengine::graphics {

__forceinline constexpr SDL_GPUIndexElementSize IndexFormat(const resources::IndexFormat format) {
    return format == resources::IndexFormat::UInt32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT;
}

void ExecuteDrawCommands(const RGExecuteContext& ctx,
                         CameraData* camera,
                         eastl::span<const DrawCommand> draw_queue_span) {
    if (draw_queue_span.empty() || !ctx.gpu_pass) return;

    eastl::vector<DrawCommand> draw_queue(draw_queue_span.begin(), draw_queue_span.end());
    std::sort(draw_queue.begin(), draw_queue.end(),
              [](const DrawCommand& a, const DrawCommand& b) { return a.sorting_key < b.sorting_key; });

    SDL_GPUGraphicsPipeline* current_pipeline = nullptr;
    Material* current_material = nullptr;
    SDL_GPUBuffer* current_vertex_buffer = nullptr;
    SDL_GPUBuffer* current_index_buffer = nullptr;
    Shader* current_shader = nullptr;

    for (const auto& command : draw_queue) {
        if (!command.pipeline || !command.vertex_buffer || !command.material)
            continue;

        Shader& shader = command.material->shader();

        if (command.pipeline != current_pipeline) {
            SDL_BindGPUGraphicsPipeline(ctx.gpu_pass, command.pipeline);
            current_pipeline = command.pipeline;
            current_material = nullptr;
            current_vertex_buffer = nullptr;
            current_index_buffer = nullptr;
            current_shader = nullptr;
        }

        // Выполняется один раз при смене шейдера для связывания внешних RG-ресурсов (свет, спец. текстуры/буферы)
        if (&shader != current_shader) {
            for (const auto& binding : shader.reflection.bindings) {
                if (binding.name == reserved_names::kFrameUBO) continue;

                if (!ctx.frame_bb) continue;

                RGResourceHandle h = ctx.frame_bb->Get(binding.name_hash);
                if (!h.IsValid()) continue;

                if (binding.kind == ShaderResourceKind::StorageBufferRead ||
                    binding.kind == ShaderResourceKind::UniformBuffer) {
                    SDL_GPUBuffer* buffer = ctx.GetBuffer(h);
                    if (buffer) {
                        if (binding.stage == ShaderStage::Vertex) {
                            SDL_BindGPUVertexStorageBuffers(ctx.gpu_pass, binding.binding, &buffer, 1);
                        } else {
                            SDL_BindGPUFragmentStorageBuffers(ctx.gpu_pass, binding.binding, &buffer, 1);
                        }
                    }
                } else if (binding.kind == ShaderResourceKind::SampledTexture) {
                    SDL_GPUTexture* texture = ctx.GetTexture(h);
                    if (texture) {
                        // При необходимости привязываем сэмплируемую текстуру из Blackboard
                    }
                }
            }
            current_shader = &shader;
        }

        // Обновление FrameUBO для каждого DrawCall на основе рефлексии
        if (const auto* frame_binding = shader.reflection.FindBinding(reserved_names::kFrameUBO); frame_binding && camera) {
            struct alignas(16) FrameUBO {
                hlslpp::float4x4 view;
                hlslpp::float4x4 proj;
                hlslpp::float4x4 model;
                hlslpp::float4x4 normalMatrix;
            } ubo{};
            ubo.view = camera->view;
            ubo.proj = camera->proj;
            ubo.model = command.model_matrix;
            ubo.normalMatrix = hlslpp::transpose(hlslpp::inverse(ubo.model));

            if (frame_binding->stage == ShaderStage::Vertex) {
                SDL_PushGPUVertexUniformData(ctx.cmd_buffer, frame_binding->binding, &ubo, sizeof(FrameUBO));
            } else {
                SDL_PushGPUFragmentUniformData(ctx.cmd_buffer, frame_binding->binding, &ubo, sizeof(FrameUBO));
            }
        }

        // Биндинг параметров и текстур материала
        if (command.material != current_material) {
            if (!command.material->uniform_buffer.empty()) {
                shader.reflection.ForEachMaterialBinding([&](const ShaderReflectedBinding& binding) {
                    if (binding.kind == ShaderResourceKind::UniformBuffer) {
                        if (binding.stage == ShaderStage::Vertex) {
                            SDL_PushGPUVertexUniformData(ctx.cmd_buffer, binding.binding,
                                                         command.material->uniform_buffer.data(),
                                                         static_cast<uint32_t>(command.material->uniform_buffer.size()));
                        } else {
                            SDL_PushGPUFragmentUniformData(ctx.cmd_buffer, binding.binding,
                                                           command.material->uniform_buffer.data(),
                                                           static_cast<uint32_t>(command.material->uniform_buffer.size()));
                        }
                    }
                });
            }

            for (const auto& tex : command.material->textures) {
                TRY_CHECK(tex.texture->sampler != nullptr, "Material bound with null sampler");
                TRY_CHECK(tex.texture->texture != nullptr, "Material bound with null texture");

                SDL_GPUTextureSamplerBinding tsb{tex.texture->texture, tex.texture->sampler};
                SDL_BindGPUFragmentSamplers(ctx.gpu_pass, tex.slot, &tsb, 1);
            }
            current_material = command.material;
        }

        if (command.vertex_buffer != current_vertex_buffer) {
            SDL_GPUBufferBinding vb = {command.vertex_buffer, 0};
            SDL_BindGPUVertexBuffers(ctx.gpu_pass, 0, &vb, 1);
            current_vertex_buffer = command.vertex_buffer;
        }

        if (command.index_buffer != current_index_buffer) {
            SDL_GPUBufferBinding ib = {command.index_buffer, 0};
            SDL_GPUIndexElementSize index_size = IndexFormat(command.index_format);

            SDL_BindGPUIndexBuffer(ctx.gpu_pass, &ib, index_size);
            current_index_buffer = command.index_buffer;
        }

        SDL_DrawGPUIndexedPrimitives(ctx.gpu_pass, command.num_indices, 1, 0, 0, 0);
    }
}

}  // namespace tryengine::graphics