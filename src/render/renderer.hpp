#pragma once

#include "SDL3/SDL_gpu.h"

class Renderer {
   public:
    SDL_GPUShader* CreateVertexShader(SDL_GPUDevice& device);
    SDL_GPUShader* CreateFragmentShader(SDL_GPUDevice& device);
    SDL_GPUTexture* CreateDepthTexture(SDL_GPUDevice& device, Uint32 width, Uint32 height);
    void SetupVertexAttributes(SDL_GPUVertexAttribute* attributes);
    void SetupColorTargetDescription(SDL_GPUColorTargetDescription* colorTargetDesc, SDL_GPUDevice* device,
                                     SDL_Window* window);
    SDL_GPUSampler* CreateSampler(SDL_GPUDevice& device);
    SDL_GPUVertexBufferDescription CreateVertexBufferDescription();
    SDL_GPUGraphicsPipeline* CreateGraphicsPipeline(SDL_GPUDevice& device, SDL_GPUShader* vertexShader,
                                                    SDL_GPUShader* fragmentShader,
                                                    const SDL_GPUVertexBufferDescription* vertexBufferDesc,
                                                    const SDL_GPUVertexAttribute* vertexAttributes,
                                                    const SDL_GPUColorTargetDescription* colorTargetDesc);
};
