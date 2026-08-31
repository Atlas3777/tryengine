#pragma once

#include <hlsl++.h>

#include "RuntimeTypes.hpp"

namespace tryengine::graphics {

struct CameraData {
    hlslpp::float4x4 view;
    hlslpp::float4x4 proj;
    hlslpp::float3 position;
};

struct AmbientSettings {
    hlslpp::float4 ambient_color{0.05f, 0.05f, 0.08f, 1.0f};
    hlslpp::float4 clear_color{0.1f, 0.1f, 0.12f, 1.0f};
};

struct alignas(16) PointLightGPU {
    hlslpp::float4 position_radius;  // xyz = position, w = radius
    hlslpp::float4 color_intensity;  // rgb = color,    w = intensity
};

struct GlobalLightUniforms {
    hlslpp::float4 ambient_color;
    hlslpp::float4 view_pos;
};

struct DrawCommand {
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