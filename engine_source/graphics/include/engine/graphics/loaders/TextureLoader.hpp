#pragma once

#include <SDL3/SDL_gpu.h>

#include "engine/resources/ResourceManager.hpp"
#include "engine/resources/TextureBinary.hpp"
#include "engine/resources/TextureTypes.hpp"

namespace tryengine::graphics {

constexpr SDL_GPUTextureFormat FormatTo(resources::TextureFormat format) {
    return static_cast<SDL_GPUTextureFormat>(format);
}

constexpr SDL_GPUTextureType TypeTo(resources::TextureType format) {
    return static_cast<SDL_GPUTextureType>(format);
}

class TextureLoader {
public:
    explicit TextureLoader(SDL_GPUDevice* device) : device_(device) {}

    async::Task<TextureSampler> Parse(eastl::span<const uint8_t> data) const {
        auto texture_result = resources::TextureBinary::UnpackFull(data);

        if (!texture_result.has_value())
            co_return LogAndMakeError("Texture unpack failed", texture_result.error().Message());

        auto& [header, mips] = *texture_result;

        if (mips.empty())
            co_return LogAndMakeError("Texture has no mip levels");

        co_await tryengine::async::ExecutorSwitch(tryengine::async::MainThread());

        TextureSampler gpu_texture;

        // 1. Создаем текстуру
        SDL_GPUTextureCreateInfo info{};
        info.type = TypeTo(header.type);
        info.format = FormatTo(header.format);
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = header.width;
        info.height = header.height;
        info.sample_count = SDL_GPU_SAMPLECOUNT_8;

        // Закрытие TODO: для 3D текстур это глубина, для остальных - количество слоев/граней
        info.layer_count_or_depth =
            (header.type == resources::TextureType::TEXTURETYPE_3D) ? header.depth : header.array_size;
        info.num_levels = header.mip_count;
        info.sample_count = static_cast<SDL_GPUSampleCount>(header.msaa_count);

        gpu_texture.texture = SDL_CreateGPUTexture(device_, &info);
        if (!gpu_texture.texture)
            co_return LogAndMakeError("Failed to create GPU texture");

        // 2. Считаем общий размер всех mip-уровней для Transfer Buffer
        Uint64 total_transfer_size = 0;
        for (const auto& mip : mips) {
            total_transfer_size += mip.size;
        }

        // 3. Создаем и заполняем Transfer Buffer
        SDL_GPUTransferBufferCreateInfo trans_info{};
        trans_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        trans_info.size = static_cast<Uint32>(total_transfer_size);

        SDL_GPUTransferBuffer* transfer_buffer = SDL_CreateGPUTransferBuffer(device_, &trans_info);
        if (!transfer_buffer) {
            SDL_ReleaseGPUTexture(device_, gpu_texture.texture);
            co_return LogAndMakeError("Failed to create transfer buffer");
        }

        Uint8* map = static_cast<Uint8*>(SDL_MapGPUTransferBuffer(device_, transfer_buffer, false));

        Uint64 current_offset = 0;
        for (const auto& mip : mips) {
            std::memcpy(map + current_offset, mip.data.data(), mip.size);
            current_offset += mip.size;
        }
        SDL_UnmapGPUTransferBuffer(device_, transfer_buffer);

        // 4. Загружаем данные на GPU через Copy Pass
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);

        current_offset = 0;
        for (Uint32 i = 0; i < mips.size(); ++i) {
            const auto& mip = mips[i];

            SDL_GPUTextureTransferInfo src{};
            src.transfer_buffer = transfer_buffer;
            src.offset = static_cast<Uint32>(current_offset);

            // ВАЖНО: pixels_per_row и rows_per_layer = 0.
            // Это указывает SDL, что данные tightly packed. SDL сам рассчитает правильный
            // row pitch (с учетом блочного сжатия BCn) на основе w, h и формата текстуры.
            src.pixels_per_row = 0;
            src.rows_per_layer = 0;

            SDL_GPUTextureRegion dst{};
            dst.texture = gpu_texture.texture;
            dst.mip_level = i;
            dst.layer = 0;  // Для массивов/кубов нужен вложенный цикл по слоям, для 2D оставляем 0
            dst.x = 0;
            dst.y = 0;
            dst.z = 0;
            dst.w = mip.width;
            dst.h = mip.height;
            dst.d = mip.depth;

            SDL_UploadToGPUTexture(copy, &src, &dst, false);

            current_offset += mip.size;
        }

        SDL_EndGPUCopyPass(copy);
        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(device_, transfer_buffer);

        // 5. Сэмплер (создаем базовый дефолтный, чтобы структура была валидной)
        SDL_GPUSamplerCreateInfo sampler_info{};
        sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
        sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
        sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        sampler_info.mip_lod_bias = 0;
        sampler_info.max_lod = mips.size();
        sampler_info.min_lod = 0;
        // sampler_info.enable_anisotropy = true;
        // sampler_info.max_anisotropy = mips.size();

        gpu_texture.sampler = SDL_CreateGPUSampler(device_, &sampler_info);
        gpu_texture.slot = 0;

        co_return gpu_texture;
    }

private:
    SDL_GPUDevice* device_;
};

}  // namespace tryengine::graphics