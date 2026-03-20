#pragma once
#include <SDL3/SDL_gpu.h>
#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>

namespace engine {

inline namespace types {  // public, header-only API
// math
using vec2 = glm::vec2;
using vec3 = glm::vec3;
using vec4 = glm::vec4;
using mat3 = glm::mat3;
using mat4 = glm::mat4;
using quat = glm::quat;

// basic ids/aliases
using int32 = std::int32_t;
using int64 = std::int64_t;
using uint32 = std::uint32_t;
using uint64 = std::uint64_t;

}  // namespace types

struct Vertex {
    float x, y, z;
    float nx, ny, nz;
    float r, g, b, a;
    float u, v;
};

// Обертка над текстурой GPU
struct Texture {
    SDL_GPUTexture* handle = nullptr;
    Uint32 width = 0;
    Uint32 height = 0;
    std::string path;  // для отладки
};

// Меш — это геометрия + материал (текстура)
struct Mesh {
    SDL_GPUBuffer* vertexBuffer = nullptr;
    SDL_GPUBuffer* indexBuffer = nullptr;
    Uint32 numIndices = 0;
    Texture* texture = nullptr;  // Ссылка на текстуру (не копия!)
    glm::vec3 localMin;
    glm::vec3 localMax;
};

struct LightUniforms {
    glm::vec4 lightPos;    // Позиция света (w не используем)
    glm::vec4 lightColor;  // Цвет света (w можно использовать как интенсивность)
    glm::vec4 viewPos;     // Позиция камеры (понадобится для бликов/Specular, добавим сразу)
};

}  // namespace engine
