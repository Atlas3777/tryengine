#pragma once
#include <entt/entity/registry.hpp>
#include <memory>

#include "RenderPipeline.hpp"
#include "RenderPreprocessor.hpp"
#include "RenderTarget.hpp"
#include "Renderer.hpp"

namespace tryengine::graphics {
class RenderSystem {
   public:
    RenderSystem(SDL_GPUDevice* device) : device_(device) {
        renderer_ = std::make_unique<Renderer>();
        renderer_->Init(device);
    };
    ~RenderSystem() {
        renderer_->Cleanup();
    };

    void RenderScene(entt::registry& reg, entt::entity camera_entity, RenderTarget* target, SDL_GPUCommandBuffer* cmd);

    Renderer& GetRenderer() const { return *renderer_; }

   private:
    SDL_GPUDevice* device_ = nullptr;

    std::unique_ptr<RenderPreprocessor> render_preprocessor_;
    std::unique_ptr<Renderer> renderer_;
    std::unique_ptr<RenderPipeline> render_pipeline_;
};
}  // namespace tryengine::graphics