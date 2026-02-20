// render/renderer.hpp (Добавления в класс)
#pragma once
#include <SDL3/SDL_gpu.h>

#include "core/window_manager.hpp"

struct FrameContext {
    SDL_GPUCommandBuffer* cmd = nullptr;
    SDL_GPUTexture* swapchainTexture = nullptr;
    SDL_GPUColorTargetInfo colorTargetInfo{};
    SDL_GPUDepthStencilTargetInfo depthTargetInfo{};
    SDL_GPURenderPass* pass = nullptr;
    Uint32 w = 0, h = 0;
};
class Renderer {
   public:
    void Init(WindowManager& windowManager);
    void Cleanup();

    FrameContext BeginFrame();
    void EndFrame(FrameContext& ctx);

    SDL_GPUGraphicsPipeline* GetDefaultPipeline() const { return pipeline; }
    SDL_GPUSampler* GetCommonSampler() const { return commonSampler; }
    SDL_GPUDevice* GetDevice() const { return device; }
    SDL_GPUTexture* sceneTexture = nullptr;

   private:
    SDL_GPUDevice* device = nullptr;
    SDL_Window* window = nullptr;

    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUTexture* depthTexture = nullptr;
    SDL_GPUSampler* commonSampler = nullptr;

    SDL_GPUShader* CreateVertexShader(SDL_GPUDevice& device);
    SDL_GPUShader* CreateFragmentShader(SDL_GPUDevice& device);
    SDL_GPUTexture* CreateDepthTexture(SDL_GPUDevice& device, Uint32 width, Uint32 height);
    SDL_GPUSampler* CreateSampler(SDL_GPUDevice& device);
    void SetupVertexAttributes(SDL_GPUVertexAttribute* attributes);
    void SetupColorTargetDescription(SDL_GPUColorTargetDescription* colorTargetDesc, SDL_GPUDevice* device,
                                     SDL_Window* window);
    SDL_GPUVertexBufferDescription CreateVertexBufferDescription();
    SDL_GPUGraphicsPipeline* CreateGraphicsPipeline(SDL_GPUDevice& device, SDL_GPUShader* vertexShader,
                                                    SDL_GPUShader* fragmentShader,
                                                    const SDL_GPUVertexBufferDescription* vertexBufferDesc,
                                                    const SDL_GPUVertexAttribute* vertexAttributes,
                                                    const SDL_GPUColorTargetDescription* colorTargetDesc);
};
