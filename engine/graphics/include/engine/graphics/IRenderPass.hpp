#pragma once

#include <entt/entity/registry.hpp>
#include <SDL3/SDL_gpu.h>

namespace engine::graphics {
// Базовый класс для любого этапа рендеринга
class IRenderPass {
public:
    virtual ~IRenderPass() = default;
    virtual void Execute(SDL_GPUCommandBuffer* cmd, entt::registry& reg, const RenderProfile& profile) = 0;
};
}