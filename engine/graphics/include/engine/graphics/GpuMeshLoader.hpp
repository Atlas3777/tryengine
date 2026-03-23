#pragma once

#include <memory>

#include "engine/graphics/Types.hpp"
#include "engine/resources/ResourceManager.hpp"
#include "engine/resources/Types.hpp"

namespace engine::graphics {
class GpuMeshLoader {
   public:
    using result_type = std::shared_ptr<Mesh>;

    explicit GpuMeshLoader(resources::ResourceManager& res, SDL_GPUDevice* device) : resManager(&res), device(device) {}

    result_type operator()(const std::string& path) const {
        const auto mesh = resManager->Get<resources::MeshData>(1);

        auto gpuMesh = std::shared_ptr<Mesh>(new Mesh(), [device = this->device](const Mesh* m) {
            // Эта лямбда вызовется автоматически, когда ресурс удалится из кэша и сцены!
            if (m->vertexBuffer) {
                SDL_ReleaseGPUBuffer(device, m->vertexBuffer);
            }
            if (m->indexBuffer) {
                SDL_ReleaseGPUBuffer(device, m->indexBuffer);
            }
            delete m;  // Очищаем саму структуру Mesh из оперативной памяти
        });

        gpuMesh->numIndices = mesh->indexBuffer.size();
        const Uint32 vSize = mesh->vertexBuffer.size() * sizeof(resources::Vertex);
        const Uint32 iSize = mesh->indexBuffer.size() * sizeof(Uint32);

        SDL_GPUBufferCreateInfo vInfo{};
        vInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        vInfo.size = vSize;

        gpuMesh->vertexBuffer = SDL_CreateGPUBuffer(device, &vInfo);

        SDL_GPUBufferCreateInfo iInfo{};
        iInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        iInfo.size = iSize;
        gpuMesh->indexBuffer = SDL_CreateGPUBuffer(device, &iInfo);

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
        const SDL_GPUBufferRegion bufferReg{gpuMesh->vertexBuffer, 0, vSize};
        SDL_UploadToGPUBuffer(copy, &transferBuffer, &bufferReg, false);

        const SDL_GPUTransferBufferLocation transferBuffer2{tBuf, vSize};
        const SDL_GPUBufferRegion bufferReg2{gpuMesh->indexBuffer, 0, iSize};
        SDL_UploadToGPUBuffer(copy, &transferBuffer2,&bufferReg2, false);

        SDL_EndGPUCopyPass(copy);

        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(device, tBuf);

        return gpuMesh;
    }

   private:
    resources::ResourceManager* resManager;
    SDL_GPUDevice* device;
};
}  // namespace engine::graphics