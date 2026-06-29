#pragma once

#include <SDL3/SDL_gpu.h>
#include <glm/glm.hpp>

#include "Types.hpp"

namespace tryengine::graphics {

// Данные камеры, не привязанные к ECS
struct CameraData {
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec3 position;
};

// Структура минимального уровня освещения (когда ламп нет)
struct AmbientSettings {
    glm::vec4 ambient_color{0.05f, 0.05f, 0.08f, 1.0f};
    glm::vec4 clear_color {0.1f, 0.1f, 0.12f, 1.0f};
};

struct alignas(16) PointLightGPU {
    glm::vec4 position_radius; // xyz = position, w = radius
    glm::vec4 color_intensity; // rgb = color,    w = intensity
}; // Ровно 32 байта и в C++, и на GPU

struct GlobalLightUniforms {
    glm::vec4 ambient_color;          // 16 байт
    glm::vec4 view_pos;               // 16 байт
};

// Главная структура команды отрисовки
struct DrawCommand {
    uint64_t sorting_key;

    // Геометрия
    SDL_GPUBuffer* vertex_buffer = nullptr;
    SDL_GPUBuffer* index_buffer = nullptr;
    uint32_t num_indices = 0;

    // Материал и Пайплайн
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    Material* material = nullptr; // Ссылка на наш материал из Types.hpp

    // Трансформ конкретного объекта
    glm::mat4 model_matrix;
};

} // namespace tryengine::graphics