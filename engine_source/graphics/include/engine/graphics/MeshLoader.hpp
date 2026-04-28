#pragma once

#include <memory>

#include "engine/core/ResourceManager.hpp"
#include "engine/graphics/Types.hpp"
#include "engine/resources/Types.hpp"

namespace tryengine::graphics {
class MeshLoader {
   public:
    using result_type = std::shared_ptr<Mesh>;

    explicit MeshLoader(core::ResourceManager& res, SDL_GPUDevice* device) : res_manager(&res), device(device) {}

    result_type operator()(uint64_t id, const std::string& path) const {
        const auto mesh = res_manager->Get<resources::MeshData>(id);

        auto gpu_mesh = std::shared_ptr<Mesh>(new Mesh(), [device = this->device](const Mesh* m) {
            // Эта лямбда вызовется автоматически, когда ресурс удалится из кэша и сцены!
            if (m->vertex_buffer) {
                SDL_ReleaseGPUBuffer(device, m->vertex_buffer);
            }
            if (m->index_buffer) {
                SDL_ReleaseGPUBuffer(device, m->index_buffer);
            }
            delete m;  // Очищаем саму структуру Mesh из оперативной памяти
        });

        gpu_mesh->num_indices = mesh->indexBuffer.size();
        const Uint32 vSize = mesh->vertexBuffer.size() * sizeof(resources::Vertex);
        const Uint32 iSize = mesh->indexBuffer.size() * sizeof(Uint32);

        SDL_GPUBufferCreateInfo vInfo{};
        vInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        vInfo.size = vSize;

        gpu_mesh->vertex_buffer = SDL_CreateGPUBuffer(device, &vInfo);

        SDL_GPUBufferCreateInfo iInfo{};
        iInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        iInfo.size = iSize;
        gpu_mesh->index_buffer = SDL_CreateGPUBuffer(device, &iInfo);

        SDL_GPUTransferBufferCreateInfo tBuff{};
        tBuff.size = vSize + iSize;
        tBuff.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        SDL_GPUTransferBuffer* tBuf = SDL_CreateGPUTransferBuffer(device, &tBuff);

        auto* ptr = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device, tBuf, false));
        memcpy(ptr, mesh->vertexBuffer.data(), vSize);
        memcpy(ptr + vSize, mesh->indexBuffer.data(), iSize);
        SDL_UnmapGPUTransferBuffer(device, tBuf);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);

        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);

        const SDL_GPUTransferBufferLocation transferBuffer{tBuf, 0};
        const SDL_GPUBufferRegion bufferReg{gpu_mesh->vertex_buffer, 0, vSize};
        SDL_UploadToGPUBuffer(copy, &transferBuffer, &bufferReg, false);

        const SDL_GPUTransferBufferLocation transferBuffer2{tBuf, vSize};
        const SDL_GPUBufferRegion bufferReg2{gpu_mesh->index_buffer, 0, iSize};
        SDL_UploadToGPUBuffer(copy, &transferBuffer2,&bufferReg2, false);

        SDL_EndGPUCopyPass(copy);

        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(device, tBuf);

        return gpu_mesh;
    }

   private:
    core::ResourceManager* res_manager;
    SDL_GPUDevice* device;
};
}  // namespace tryengine::graphics