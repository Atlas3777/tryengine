#pragma once
#include <entt/entity/registry.hpp>
#include <memory>

#include "engine/graphics/PipelineManager.hpp"
#include "engine/graphics/RenderPipeline.hpp"
#include "engine/graphics/RenderPreprocessor.hpp"
#include "engine/graphics/RenderTarget.hpp"
#include "engine/graphics/Renderer.hpp"

namespace tryengine::graphics {
class RenderSystem {
public:
    RenderSystem(SDL_GPUDevice* device) : device_(device) {
        pipeline_manager_ = std::make_unique<PipelineManager>(device);
    };

    void RenderScene(entt::registry& reg, entt::entity camera_entity, RenderTarget* target, SDL_GPUCommandBuffer* cmd) const;

    // Renderer& GetRenderer() const { return *renderer_; }

private:
    SDL_GPUDevice* device_ = nullptr;

    std::unique_ptr<PipelineManager> pipeline_manager_;
    std::unique_ptr<RenderPreprocessor> render_preprocessor_;
    std::unique_ptr<RenderPipeline> render_pipeline_;
};
}  // namespace tryengine::graphics
