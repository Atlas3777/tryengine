#pragma once
#include <SDL3/SDL_gpu.h>
#include <memory>

namespace tryengine::graphics {

struct Texture {
    SDL_GPUTexture* handle;
    uint32_t width;
    uint32_t height;
};

struct Mesh {
    SDL_GPUBuffer* vertex_buffer;
    SDL_GPUBuffer* index_buffer;
    uint32_t num_indices;
};

}
