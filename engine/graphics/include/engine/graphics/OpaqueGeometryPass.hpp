#pragma once
#include <SDL3/SDL_gpu.h>
#include <entt/entity/registry.hpp>

#include "engine/graphics/RenderProfile.hpp"
#include "engine/graphics/IRenderPass.hpp"

namespace engine::graphics{

class OpaqueGeometryPass : public IRenderPass {
public:
    void Execute(SDL_GPUCommandBuffer* cmd, entt::registry& reg, const RenderProfile& profile) override {
        // Здесь мы вызываем SDL_BeginGPURenderPass
        // Здесь мы делаем SDL_BindGPUGraphicsPipeline
        // И здесь же цикл по entt::registry для отрисовки мешей
    }
};

}