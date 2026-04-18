#pragma once

#include <unordered_map>
#include "PipelineDescriptor.hpp"
#include <SDL3/SDL_gpu.h>
#include "engine/resources/Types.hpp" // Для sizeof(resources::Vertex)

namespace tryengine::graphics {

class PipelineManager {
public:
    PipelineManager(SDL_GPUDevice* device)  : device(device) {};

    SDL_GPUGraphicsPipeline* GetOrCreatePipeline(const PipelineDescriptor& desc) {
        uint32_t hash = desc.GetHashCode();

        // Если пайплайн уже создан, возвращаем его
        auto it = pipeline_cache_.find(hash);
        if (it != pipeline_cache_.end()) {
            return it->second;
        }

        // --- ХАРДКОД ВЕРШИН (из твоего старого кода) ---
        SDL_GPUVertexBufferDescription vertex_buffer_description{};
        vertex_buffer_description.slot = 0;
        vertex_buffer_description.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
        vertex_buffer_description.instance_step_rate = 0;
        vertex_buffer_description.pitch = sizeof(resources::Vertex);

        SDL_GPUVertexAttribute vertex_attributes[4]{};
        // Position
        vertex_attributes[0].buffer_slot = 0;
        vertex_attributes[0].location = 0;
        vertex_attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        vertex_attributes[0].offset = 0;
        // Normal
        vertex_attributes[1].buffer_slot = 0;
        vertex_attributes[1].location = 1;
        vertex_attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        vertex_attributes[1].offset = sizeof(float) * 3;
        // Color
        vertex_attributes[2].buffer_slot = 0;
        vertex_attributes[2].location = 2;
        vertex_attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        vertex_attributes[2].offset = sizeof(float) * 6;
        // UV
        vertex_attributes[3].buffer_slot = 0;
        vertex_attributes[3].location = 3;
        vertex_attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        vertex_attributes[3].offset = sizeof(float) * 10;

        // --- ПЕРЕНОС ДАННЫХ ИЗ ДЕСКРИПТОРА ---
        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};

        // 1. Шейдеры
        pipelineInfo.vertex_shader = desc.vertex_shader;
        pipelineInfo.fragment_shader = desc.fragment_shader;

        // 2. Растеризатор
        pipelineInfo.primitive_type = desc.primitive_type;
        pipelineInfo.rasterizer_state.fill_mode = desc.fill_mode;
        pipelineInfo.rasterizer_state.cull_mode = desc.cull_mode;
        pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE; // Оставляем стандарт

        // 3. Вершины
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer_description;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 4;
        pipelineInfo.vertex_input_state.vertex_attributes = vertex_attributes;

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

        pipelineInfo.target_info.num_color_targets = 1;
        pipelineInfo.target_info.color_target_descriptions = &colorTargetDesc;

        // 5. Глубина
        pipelineInfo.target_info.has_depth_stencil_target = true;
        pipelineInfo.target_info.depth_stencil_format = desc.depth_stencil_format;
        pipelineInfo.depth_stencil_state.enable_depth_test = desc.enable_depth_test;
        pipelineInfo.depth_stencil_state.enable_depth_write = desc.enable_depth_write;
        pipelineInfo.depth_stencil_state.compare_op = desc.depth_compare_op;

        // --- СОЗДАНИЕ ---
        SDL_GPUGraphicsPipeline* new_pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo);

        if (new_pipeline) {
            pipeline_cache_[hash] = new_pipeline;
        }

        return new_pipeline;
    }

private:
    SDL_GPUDevice* device = nullptr;
    std::unordered_map<uint32_t, SDL_GPUGraphicsPipeline*> pipeline_cache_;
};

} // namespace tryengine::graphics