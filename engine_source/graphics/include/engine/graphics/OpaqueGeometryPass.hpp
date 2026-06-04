#pragma once

#include <SDL3/SDL_gpu.h>

#include "engine/graphics/IRenderPass.hpp"
#include "engine/graphics/RenderProfile.hpp"

namespace tryengine::graphics {

class OpaqueGeometryPass : public IRenderPass {
public:
    void Execute(SDL_GPUCommandBuffer* cmd, const RenderProfile& profile) override {
        // Здесь мы вызываем SDL_BeginGPURenderPass
        // Здесь мы делаем SDL_BindGPUGraphicsPipeline
        // И здесь же цикл по entt::registry для отрисовки мешей
    }
};

}  // namespace tryengine::graphics