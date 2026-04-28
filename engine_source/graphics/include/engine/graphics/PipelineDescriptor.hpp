#pragma once

#include <SDL3/SDL_gpu.h>
#include <cstring>

namespace tryengine::graphics {

struct alignas(8) PipelineDescriptor {
    // 1. Шейдеры (Обязательно)
    SDL_GPUShader* vertex_shader = nullptr;
    SDL_GPUShader* fragment_shader = nullptr;

    // 2. Растеризатор и Топология
    SDL_GPUPrimitiveType primitive_type;
    SDL_GPUFillMode fill_mode;
    SDL_GPUCullMode cull_mode;

    // 3. Глубина
    bool enable_depth_test;
    bool enable_depth_write;
    SDL_GPUCompareOp depth_compare_op;

    // 4. Блендинг (Упрощенно: вкл/выкл. Если true, юзаем твой стандартный Alpha Blend)
    bool enable_blend;

    // 5. Форматы таргетов
    SDL_GPUTextureFormat color_target_format;
    SDL_GPUTextureFormat depth_stencil_format;

    PipelineDescriptor() {
        // ВАЖНО: Сначала зануляем мусор в памяти (padding)
        std::memset(this, 0, sizeof(PipelineDescriptor));

        // Затем ставим значения по умолчанию (как было в твоем Renderer.cpp)
        primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        fill_mode = SDL_GPU_FILLMODE_FILL;
        cull_mode = SDL_GPU_CULLMODE_NONE; // Для 3D потом поменяешь на BACK

        enable_depth_test = true;
        enable_depth_write = true;
        depth_compare_op = SDL_GPU_COMPAREOP_LESS;

        enable_blend = true;

        color_target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
    }

    bool operator==(const PipelineDescriptor& other) const {
        return std::memcmp(this, &other, sizeof(PipelineDescriptor)) == 0;
    }

    uint32_t GetHashCode() const {
        uint32_t hash = 2166136261u;
        const uint8_t* data = reinterpret_cast<const uint8_t*>(this);
        for (size_t i = 0; i < sizeof(PipelineDescriptor); ++i) {
            hash ^= data[i];
            hash *= 16777619;
        }
        return hash;
    }
};

} // namespace tryengine::graphics