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
    glm::vec4 ambient_color{0.05f, 0.05f, 0.08f, 1.0f}; // Слегка синеватый ночной эмбиент по умолчанию
};

// Типы источников света
enum class LightType : uint32_t {
    Directional = 0,
    Point = 1
};

struct Light {
    LightType type = LightType::Directional;
    glm::vec3 position{0.0f};
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float radius = 10.0f; // Для Point Light
};

// То, что летит в GPU Fragment Shader (Слот 0)
constexpr size_t MAX_LIGHTS = 8;

struct GPULightData {
    glm::vec4 position_type; // xyz = position/direction, w = type (0.0f = Dir, 1.0f = Point)
    glm::vec4 color_radius;  // rgb = color * intensity, w = radius
};

struct GlobalLightUniforms {
    glm::vec4 ambient_color;          // 16 байт
    glm::vec4 view_pos;               // 16 байт
    uint32_t light_count;             // 4 байта
    float padding[3];                 // 12 байт (выравнивание структуры до 16 байт перед массивом)
    GPULightData lights[MAX_LIGHTS];  // 8 * 32 байта
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