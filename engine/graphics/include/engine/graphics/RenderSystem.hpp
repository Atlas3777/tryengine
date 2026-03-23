#pragma once
#include <entt/entity/registry.hpp>
#include <memory>

#include "GpuResourceFactory.hpp"
#include "RenderPipeline.hpp"
#include "RenderPreprocessor.hpp"
#include "RenderTarget.hpp"

namespace engine::graphics {
class RenderSystem {
   public:
    RenderSystem(SDL_GPUDevice* device) { m_device = device; };

    void RenderScene(entt::registry& reg, entt::entity camera, RenderTarget* target);


   private:
    std::unique_ptr<RenderPreprocessor> RenderPreprocessor;
    std::unique_ptr<RenderPipeline> RenderPipeline;
    std::unique_ptr<GpuResourceFactory> m_resourceFactory;

    SDL_GPUDevice* m_device = nullptr;
};
}  // namespace engine::graphics