#pragma once

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>
#include <cstring>
#include <memory>

#include "engine/core/ResourceManager.hpp"
#include "engine/graphics/Types.hpp"
#include "engine/resources/Types.hpp"

namespace tryengine::graphics {

class GpuTextureLoader {
public:
    using result_type = std::shared_ptr<Texture>;

    explicit GpuTextureLoader(core::ResourceManager& res, SDL_GPUDevice* device)
        : resource_manager_(&res), device(device) {}

    result_type operator()(uint64_t id, const std::string& path) const {
        const auto texture_data = resource_manager_->Get<resources::TextureData>(id);

        if (!texture_data || texture_data->pixels.empty()) {
            SDL_Log("GpuTextureLoader: Failed to load texture data for ID %llu", id);
            return nullptr;
        }

        auto gpu_texture = std::shared_ptr<Texture>(new Texture(), [device = this->device](const Texture* t) {
            if (t->handle) {
                SDL_ReleaseGPUTexture(device, t->handle);
            }
            delete t;
        });

        gpu_texture->width = texture_data->width;
        gpu_texture->height = texture_data->height;

        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = gpu_texture->width;
        info.height = gpu_texture->height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;

        gpu_texture->handle = SDL_CreateGPUTexture(device, &info);

        if (!gpu_texture->handle) {
            SDL_Log("GpuTextureLoader: Failed to create GPU texture.");
            return nullptr;
        }

        Uint32 data_size = static_cast<Uint32>(texture_data->pixels.size());

        SDL_GPUTransferBufferCreateInfo tInfo{};
        tInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tInfo.size = data_size;

        SDL_GPUTransferBuffer* tBuf = SDL_CreateGPUTransferBuffer(device, &tInfo);

        // 3. Копируем пиксели из TextureData в Transfer Buffer
        Uint8* map = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device, tBuf, false));
        std::memcpy(map, texture_data->pixels.data(), data_size);
        SDL_UnmapGPUTransferBuffer(device, tBuf);

        // 4. Отправляем команды на копирование из Transfer Buffer в саму текстуру
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTextureTransferInfo src = {tBuf, 0, 0, 0};
        SDL_GPUTextureRegion dst = {gpu_texture->handle, 0, 0, 0, 0, 0, gpu_texture->width, gpu_texture->height, 1};

        SDL_UploadToGPUTexture(copy, &src, &dst, false);

        SDL_EndGPUCopyPass(copy);
        SDL_SubmitGPUCommandBuffer(cmd);

        SDL_ReleaseGPUTransferBuffer(device, tBuf);

        return gpu_texture;
    }

private:
    core::ResourceManager* resource_manager_;
    SDL_GPUDevice* device;
};

}  // namespace tryengine::graphics