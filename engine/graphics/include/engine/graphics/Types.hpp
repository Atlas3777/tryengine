#pragma once
#include <SDL3/SDL_gpu.h>
#include <entt/core/fwd.hpp>
#include <entt/resource/resource.hpp>
#include <memory>

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

struct MeshComponent {
    entt::resource<Mesh> mesh;
    entt::id_type resourceID;
};
}