#pragma once

#include "engine/graphics/RenderCommon.hpp"

namespace tryengine::graphics {

struct MeshDrawCall {
    hlslpp::float4x4 model_matrix;

    SDL_GPUBuffer* vertex_buffer = nullptr;
    SDL_GPUBuffer* index_buffer = nullptr;

    SDL_GPUGraphicsPipeline* pipeline = nullptr;

    Material* material = nullptr;
    uint64_t sorting_key;

    uint32_t num_indices = 0;

    resources::IndexFormat index_format = resources::IndexFormat::None;
};

}  // namespace tryengine::graphics