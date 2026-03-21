#pragma once
#include <SDL3/SDL_gpu.h>
#include <glm/vec4.hpp>

#include "engine/core/CoreTypes.hpp"

namespace engine::graphics {

struct Texture {
    SDL_GPUTexture* handle;
    uint32_t width;
    uint32_t height;
};

struct Mesh {
    SDL_GPUBuffer* vertexBuffer;
    SDL_GPUBuffer* indexBuffer;
    uint32_t numIndices;
};
struct LightUniforms {
    glm::vec4 lightPos;    // Позиция света (w не используем)
    glm::vec4 lightColor;  // Цвет света (w можно использовать как интенсивность)
    glm::vec4 viewPos;     // Позиция камеры (понадобится для бликов/Specular, добавим сразу)
};
}