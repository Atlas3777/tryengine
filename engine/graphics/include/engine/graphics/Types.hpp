 #pragma once
#include <SDL3/SDL_gpu.h>
#include <memory>

namespace tryengine::graphics {

struct Texture {
    SDL_GPUTexture* handle = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct Mesh {
    SDL_GPUBuffer* vertex_buffer;
    SDL_GPUBuffer* index_buffer;
    uint32_t num_indices;
};

}
