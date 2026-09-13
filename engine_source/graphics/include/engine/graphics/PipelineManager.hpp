#pragma once

#include <EASTL/hash_map.h>
#include <SDL3/SDL_gpu.h>
#include <cstddef>

#include "PipelineDescriptor.hpp"
#include "engine/core/Log.hpp"
#include "engine/resources/Vertex.hpp"

namespace tryengine::graphics {

class PipelineManager {
public:
    PipelineManager(SDL_GPUDevice* device) : device(device) {};

    SDL_GPUGraphicsPipeline* GetOrCreatePipeline(const PipelineDescriptor& desc) {
        uint32_t hash = desc.GetHashCode();

        // Если пайплайн уже создан, возвращаем его
        auto it = pipeline_cache_.find(hash);
        if (it != pipeline_cache_.end()) {
            return it->second;
        }

        // --- ДИНАМИЧЕСКАЯ НАСТРОЙКА ВЕРШИН ---
        SDL_GPUVertexBufferDescription vertex_buffer_description{};
        vertex_buffer_description.slot = 0;
        vertex_buffer_description.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        vertex_buffer_description.instance_step_rate = 0;

        SDL_GPUVertexAttribute vertex_attributes[6]{};
        uint32_t num_attributes = 0;

        switch (desc.vertex_format) {
            case resources::VertexFormat::None: {
                num_attributes = 0;
                break;
            }

            // case resources::VertexFormat::Standard: {
            //     vertex_buffer_description.pitch = sizeof(resources::Vertex);
            //     num_attributes = 4;
            //
            //     // Location 0: Position
            //     vertex_attributes[0] = { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = offsetof(resources::Vertex, x) };
            //     // Location 1: Normal
            //     vertex_attributes[1] = { .location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = offsetof(resources::Vertex, nx) };
            //     // Location 2: Color
            //     vertex_attributes[2] = { .location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset = offsetof(resources::Vertex, r) };
            //     // Location 3: UV
            //     vertex_attributes[3] = { .location = 3, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(resources::Vertex, u) };
            //     break;
            // }

            case resources::VertexFormat::StaticPacked: {
                LogTrace(LogCategory::Graphics, "StaticPaced");
                vertex_buffer_description.pitch = sizeof(resources::VertexStaticPacked);
                num_attributes = 4;

                // Location 0: Position
                vertex_attributes[0] = { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = offsetof(resources::VertexStaticPacked, position) };
                // Location 1: Normal (4 байта SNORM)
                vertex_attributes[1] = { .location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM, .offset = offsetof(resources::VertexStaticPacked, normal) };
                // Location 2: Color (4 байта UNORM)
                vertex_attributes[2] = { .location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, .offset = offsetof(resources::VertexStaticPacked, color) };
                // Location 3: UV (Half float)
                vertex_attributes[3] = { .location = 3, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_HALF2, .offset = offsetof(resources::VertexStaticPacked, uv) };
                break;
            }

            case resources::VertexFormat::PositionOnly: {
                vertex_buffer_description.pitch = sizeof(resources::VertexPositionOnly);
                num_attributes = 1;

                // Location 0: Position
                vertex_attributes[0] = { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = offsetof(resources::VertexPositionOnly, x) };
                break;
            }

            case resources::VertexFormat::SkinnedPacked: {
                vertex_buffer_description.pitch = sizeof(resources::VertexSkinnedPacked);
                num_attributes = 6;

                // Location 0: Position
                vertex_attributes[0] = { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = offsetof(resources::VertexSkinnedPacked, position) };
                // Location 1: Normal
                vertex_attributes[1] = { .location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_BYTE4_NORM, .offset = offsetof(resources::VertexSkinnedPacked, normal) };
                // Location 2: Color
                vertex_attributes[2] = { .location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, .offset = offsetof(resources::VertexSkinnedPacked, color) };
                // Location 3: UV
                vertex_attributes[3] = { .location = 3, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_HALF2, .offset = offsetof(resources::VertexSkinnedPacked, uv) };
                // Location 4: Bone Indices (Целые числа 0..255)
                vertex_attributes[4] = { .location = 4, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4, .offset = offsetof(resources::VertexSkinnedPacked, boneIndices) };
                // Location 5: Bone Weights (Нормализованные веса 0.0..1.0)
                vertex_attributes[5] = { .location = 5, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, .offset = offsetof(resources::VertexSkinnedPacked, boneWeights) };
                break;
            }

            case resources::VertexFormat::Ui2D: {
                vertex_buffer_description.pitch = sizeof(resources::Vertex2D);
                num_attributes = 3;

                // Location 0: Position 2D
                vertex_attributes[0] = { .location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(resources::Vertex2D, x) };
                // Location 1: UV
                vertex_attributes[1] = { .location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(resources::Vertex2D, u) };
                // Location 2: Color (4 байта UNORM)
                vertex_attributes[2] = { .location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM, .offset = offsetof(resources::Vertex2D, r) };
                break;
            }
        }


        // --- ПЕРЕНОС ДАННЫХ ИЗ ДЕСКРИПТОРА ---
        SDL_GPUGraphicsPipelineCreateInfo pipeline_info{};

        // 1. Шейдеры
        pipeline_info.vertex_shader = desc.vertex_shader;
        pipeline_info.fragment_shader = desc.fragment_shader;

        // 2. Растеризатор
        pipeline_info.primitive_type = desc.primitive_type;
        pipeline_info.rasterizer_state.fill_mode = desc.fill_mode;
        pipeline_info.rasterizer_state.cull_mode = desc.cull_mode;
        pipeline_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;

        // 3. Вершины
        if (num_attributes > 0) {
            pipeline_info.vertex_input_state.num_vertex_buffers = 1;
            pipeline_info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer_description;
            pipeline_info.vertex_input_state.num_vertex_attributes = num_attributes;
            pipeline_info.vertex_input_state.vertex_attributes = vertex_attributes;
        } else {
            pipeline_info.vertex_input_state.num_vertex_buffers = 0;
            pipeline_info.vertex_input_state.vertex_buffer_descriptions = nullptr;
            pipeline_info.vertex_input_state.num_vertex_attributes = 0;
            pipeline_info.vertex_input_state.vertex_attributes = nullptr;
        }

        // 4. Блендинг и цвет
        SDL_GPUColorTargetDescription colorTargetDesc{};
        colorTargetDesc.format = desc.color_target_format;
        if (desc.enable_blend) {
            colorTargetDesc.blend_state.enable_blend = true;
            colorTargetDesc.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
            colorTargetDesc.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
            colorTargetDesc.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            colorTargetDesc.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            colorTargetDesc.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
            colorTargetDesc.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        }

        pipeline_info.target_info.num_color_targets = 1;
        pipeline_info.target_info.color_target_descriptions = &colorTargetDesc;

        // 5. Глубина
        pipeline_info.target_info.has_depth_stencil_target = true;
        pipeline_info.target_info.depth_stencil_format = desc.depth_stencil_format;
        pipeline_info.depth_stencil_state.enable_depth_test = desc.enable_depth_test;
        pipeline_info.depth_stencil_state.enable_depth_write = desc.enable_depth_write;
        pipeline_info.depth_stencil_state.compare_op = desc.depth_compare_op;

        // --- СОЗДАНИЕ ---
        SDL_GPUGraphicsPipeline* new_pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipeline_info);

        if (new_pipeline) {
            pipeline_cache_[hash] = new_pipeline;
            LogTrace(LogCategory::Graphics, "Pipeline create");
        }
        else
            LogError(LogCategory::Graphics, "Pipeline create failed: {}", SDL_GetError());

        return new_pipeline;
    }

private:
    SDL_GPUDevice* device = nullptr;
    eastl::hash_map<uint32_t, SDL_GPUGraphicsPipeline*> pipeline_cache_;
};

}  // namespace tryengine::graphics