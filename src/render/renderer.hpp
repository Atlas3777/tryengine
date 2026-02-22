#pragma once
#include <SDL3/SDL_gpu.h>

#include "RenderTarget.hpp"
#include "core/window_manager.hpp"

class Renderer {
   public:
    void Init(WindowManager& windowManager);
    void Cleanup();

    SDL_GPURenderPass* BeginRenderPass(SDL_GPUCommandBuffer* cmd, RenderTarget& target, SDL_FColor clearColor);
    SDL_GPURenderPass* BeginRenderToWindow(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* swapchain);
    SDL_GPUGraphicsPipeline* GetDefaultPipeline() const { return pipeline; }
    SDL_GPUSampler* GetCommonSampler() const { return commonSampler; }
    void EndRenderPath();

    // Для UI или Swapchain (где нет нашего Framebuffer объекта)
    // SDL_GPURenderPass* BeginScreenPass(SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* screenTex, SDL_FColor clearColor);

   private:
    SDL_GPUDevice* device = nullptr;
    SDL_Window* window = nullptr;

    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUSampler* commonSampler = nullptr;

    Uint32 currentTargetWidth = 0;
    Uint32 currentTargetHeight = 0;

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
