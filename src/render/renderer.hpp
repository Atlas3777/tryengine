#pragma once
#include <SDL3/SDL_gpu.h>

#include "RenderTarget.hpp"

class Renderer {
   public:
    void Init(SDL_GPUDevice* device);
    void Cleanup();

    SDL_GPURenderPass* BeginRenderPass(SDL_GPUCommandBuffer* cmd, RenderTarget& target, SDL_FColor clearColor);
    SDL_GPURenderPass* BeginRenderToWindow(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* swapchain);
    SDL_GPUGraphicsPipeline* GetDefaultPipeline() const { return pipeline; }
    SDL_GPUSampler* GetCommonSampler() const { return commonSampler; }
    void EndRenderPath();

   private:
    SDL_GPUDevice* device = nullptr;

    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUSampler* commonSampler = nullptr;

    SDL_GPUShader* CreateVertexShader(SDL_GPUDevice& device);
    SDL_GPUShader* CreateFragmentShader(SDL_GPUDevice& device);
    SDL_GPUTexture* CreateDepthTexture(SDL_GPUDevice& device, Uint32 width, Uint32 height);
    SDL_GPUSampler* CreateSampler(SDL_GPUDevice& device);
    void SetupVertexAttributes(SDL_GPUVertexAttribute* attributes);
    void SetupColorTargetDescription(SDL_GPUColorTargetDescription* desc);
    SDL_GPUVertexBufferDescription CreateVertexBufferDescription();
    SDL_GPUGraphicsPipeline* CreateGraphicsPipeline(SDL_GPUDevice& device, SDL_GPUShader* vertexShader,
                                                    SDL_GPUShader* fragmentShader,
                                                    const SDL_GPUVertexBufferDescription* vertexBufferDesc,
                                                    const SDL_GPUVertexAttribute* vertexAttributes,
                                                    const SDL_GPUColorTargetDescription* colorTargetDesc);
};
