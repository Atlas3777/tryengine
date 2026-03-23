#pragma once
#include <SDL3/SDL_gpu.h>

#include "engine/resources/Types.hpp"

namespace engine::graphics {

class GpuResourceFactory {
public:
    explicit GpuResourceFactory(SDL_GPUDevice* device);

    // Принимает твой engine::resources::TextureData и возвращает готовую для рендера текстуру
    GpuTexture* CreateTexture(const engine::resources::TextureData& data);
    
    // Аналогично для мешей (создает VertexBuffer и IndexBuffer)
    GpuMesh* CreateMesh(const engine::resources::MeshData& data);

private:
    SDL_GPUDevice* m_device;
};

}