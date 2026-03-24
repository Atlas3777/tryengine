#pragma once
#include <entt/entity/registry.hpp>
#include <memory>

#include "RenderPipeline.hpp"
#include "RenderPreprocessor.hpp"
#include "RenderTarget.hpp"
#include "Renderer.hpp"

namespace engine::graphics {
class RenderSystem {
   public:
    RenderSystem(SDL_GPUDevice* device) : m_device(device) {
        renderer_ = std::make_unique<Renderer>();
        renderer_->Init(device);
    };
    ~RenderSystem() {
        renderer_->Cleanup();
    };

    void RenderScene(entt::registry& reg, entt::entity camera_entity, RenderTarget* target, SDL_GPUCommandBuffer* cmd);

   private:
    SDL_GPUDevice* m_device = nullptr;

    std::unique_ptr<RenderPreprocessor> render_preprocessor_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<RenderPipeline> render_pipeline_;
};
}  // namespace engine::graphics