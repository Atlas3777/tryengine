#pragma once

#include <SDL3/SDL_gpu.h>

namespace tryengine::graphics {
class RenderTarget {
public:
    RenderTarget(SDL_GPUDevice* device, uint32_t w, uint32_t h, SDL_GPUTextureFormat format, bool useDepth = true)
        : device(device), width(w), height(h), color_format(format), use_depth(useDepth) {
        Create();
    }

    ~RenderTarget() { ReleaseResources(); }

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    void Resize(uint32_t w, uint32_t h) {
        if (w == width && h == height)
            return;
        width = w;
        height = h;
        ReleaseResources();
        Create();
    }

    [[nodiscard]] SDL_GPUTexture* GetColor() const { return color_texture; }
    [[nodiscard]] SDL_GPUTexture* GetDepth() const { return depth_texture; }
    [[nodiscard]] uint32_t GetWidth() const { return width; }
    [[nodiscard]] uint32_t GetHeight() const { return height; }
    [[nodiscard]] bool UseDepth() const { return use_depth; }

private:
    SDL_GPUDevice* device;
    SDL_GPUTexture* color_texture = nullptr;
    SDL_GPUTexture* depth_texture = nullptr;

    uint32_t width;
    uint32_t height;
    SDL_GPUTextureFormat color_format;
    const bool use_depth;

    void Create() {
        SDL_GPUTextureCreateInfo color_info{};
        color_info.type = SDL_GPU_TEXTURETYPE_2D;
        color_info.format = color_format;
        color_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        color_info.width = width;
        color_info.height = height;
        color_info.layer_count_or_depth = 1;
        color_info.num_levels = 1;
        color_texture = SDL_CreateGPUTexture(device, &color_info);
        if (!use_depth)
            return;

        SDL_GPUTextureCreateInfo depthInfo{};
        depthInfo.type = SDL_GPU_TEXTURETYPE_2D;
        depthInfo.format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
        depthInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        depthInfo.width = width;
        depthInfo.height = height;
        depthInfo.layer_count_or_depth = 1;
        depthInfo.num_levels = 1;
        depth_texture = SDL_CreateGPUTexture(device, &depthInfo);
    }

    void ReleaseResources() {
        if (color_texture)
            SDL_ReleaseGPUTexture(device, color_texture);
        if (depth_texture)
            SDL_ReleaseGPUTexture(device, depth_texture);
        color_texture = nullptr;
        depth_texture = nullptr;
    }
};
}  // namespace tryengine::graphics
