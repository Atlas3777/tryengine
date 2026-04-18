// engine/graphics/TextureLoader.hpp
#pragma once

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>
#include <fstream>
#include <memory>
#include <vector>

#include "engine/core/ResourceManager.hpp"
#include "engine/graphics/Types.hpp"
#include "engine/resources/Types.hpp"

namespace tryengine::graphics {

class TextureLoader {
public:
    using result_type = std::shared_ptr<Texture>;

    explicit TextureLoader(core::ResourceManager& res, SDL_GPUDevice* device)
        : resource_manager_(&res), device_(device) {}

    result_type operator()(uint64_t id, const std::string& path) const {
        // 1. Читаем бинарный артефакт (.tex)
        std::ifstream is(path, std::ios::binary);
        if (!is.is_open()) {
            SDL_Log("TextureLoader: Failed to open file %s", path.c_str());
            return nullptr;
        }

        resources::TextureHeader header{};
        if (!is.read(reinterpret_cast<char*>(&header), sizeof(resources::TextureHeader))) return nullptr;

        std::vector<uint8_t> pixels(header.data_size);
        if (!is.read(reinterpret_cast<char*>(pixels.data()), header.data_size)) return nullptr;

        // 2. Создаем структуру Texture с кастомным делетером для GPU ресурсов
        auto gpu_texture = std::shared_ptr<Texture>(new Texture(), [device = this->device_](const Texture* t) {
            if (t->handle) SDL_ReleaseGPUTexture(device, t->handle);
            if (t->sampler) SDL_ReleaseGPUSampler(device, t->sampler);
            delete t;
        });

        gpu_texture->width = header.width;
        gpu_texture->height = header.height;

        // 3. Создаем текстуру
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = gpu_texture->width;
        info.height = gpu_texture->height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;

        gpu_texture->handle = SDL_CreateGPUTexture(device_, &info);

        // 4. Создаем сэмплер на основе данных из заголовка!
        SDL_GPUSamplerCreateInfo sampler_info{};
        sampler_info.min_filter = (header.min_filter == resources::TextureFilter::Linear) ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        sampler_info.mag_filter = (header.mag_filter == resources::TextureFilter::Linear) ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;

        auto map_address_mode = [](resources::TextureAddressMode mode) {
            if (mode == resources::TextureAddressMode::ClampToEdge) return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            if (mode == resources::TextureAddressMode::MirroredRepeat) return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
            return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        };
        sampler_info.address_mode_u = map_address_mode(header.address_u);
        sampler_info.address_mode_v = map_address_mode(header.address_v);
        sampler_info.address_mode_w = sampler_info.address_mode_u;

        gpu_texture->sampler = SDL_CreateGPUSampler(device_, &sampler_info);

        // 5. Загружаем пиксели через Transfer Buffer
        SDL_GPUTransferBufferCreateInfo tInfo{};
        tInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        tInfo.size = header.data_size;

        SDL_GPUTransferBuffer* tBuf = SDL_CreateGPUTransferBuffer(device_, &tInfo);
        Uint8* map = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device_, tBuf, false));
        std::memcpy(map, pixels.data(), header.data_size);
        SDL_UnmapGPUTransferBuffer(device_, tBuf);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTextureTransferInfo src = {tBuf, 0, 0, 0};
        SDL_GPUTextureRegion dst = {gpu_texture->handle, 0, 0, 0, 0, 0, gpu_texture->width, gpu_texture->height, 1};

        SDL_UploadToGPUTexture(copy, &src, &dst, false);

        SDL_EndGPUCopyPass(copy);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(device_, tBuf);

        return gpu_texture;
    }

private:
    core::ResourceManager* resource_manager_;
    SDL_GPUDevice* device_;
};

}  // namespace tryengine::graphics