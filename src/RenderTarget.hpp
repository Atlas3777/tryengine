#pragma once

#include "SDL3/SDL_gpu.h"

class RenderTarget {
   public:
    RenderTarget(SDL_GPUDevice* device, uint32_t w, uint32_t h, SDL_GPUTextureFormat format)
        : device(device), width(w), height(h), colorFormat(format) {
        Create();
    }

    ~RenderTarget() { ReleaseResources(); }

    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    void Resize(uint32_t w, uint32_t h) {
        if (w == width && h == height) return;

        width = w;
        height = h;
        // Освобождаем старое и создаем новое
        ReleaseResources();
        Create();
    }

    // Геттеры для использования в рендере
    SDL_GPUTexture* GetColor() const { return colorTexture; }
    SDL_GPUTexture* GetDepth() const { return depthTexture; }
    uint32_t GetWidth() const { return width; }
    uint32_t GetHeight() const { return height; }

   private:
    SDL_GPUDevice* device;
    SDL_GPUTexture* colorTexture = nullptr;
    SDL_GPUTexture* depthTexture = nullptr;

    uint32_t width;
    uint32_t height;
    SDL_GPUTextureFormat colorFormat;

    void Create() {
        SDL_GPUTextureCreateInfo colorInfo{};
        colorInfo.type = SDL_GPU_TEXTURETYPE_2D;
        colorInfo.format = colorFormat;
        colorInfo.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
        colorInfo.width = width;
        colorInfo.height = height;
        colorInfo.layer_count_or_depth = 1;
        colorInfo.num_levels = 1;
        colorTexture = SDL_CreateGPUTexture(device, &colorInfo);

        SDL_GPUTextureCreateInfo depthInfo{};
        depthInfo.type = SDL_GPU_TEXTURETYPE_2D;
        depthInfo.format = SDL_GPU_TEXTUREFORMAT_D16_UNORM;
        depthInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        depthInfo.width = width;
        depthInfo.height = height;
        depthInfo.layer_count_or_depth = 1;
        depthInfo.num_levels = 1;
        depthTexture = SDL_CreateGPUTexture(device, &depthInfo);
    }

    void ReleaseResources() {
        if (colorTexture) SDL_ReleaseGPUTexture(device, colorTexture);
        if (depthTexture) SDL_ReleaseGPUTexture(device, depthTexture);
        colorTexture = nullptr;
        depthTexture = nullptr;
    }
};
