#pragma once
#include <memory>

#include "EditorLayer.hpp"
#include "EngineConfig.hpp"
#include "EngineContext.hpp"
#include "RenderTarget.hpp"

class Engine {
   public:
    Engine(EngineConfig config) {};
    void MountHardware();
    void ProcessInput();

    using RenderCallback = std::function<void(SDL_GPUCommandBuffer* cmd, RenderTarget* target)>;

    void Render(entt::registry& reg, RenderCallback userRenderFunc);
    std::unique_ptr<EngineContext> context;

    const GraphicsContext& GetGraphicsContext() const { return *graphicsContext; }

   private:
    std::unique_ptr<EditorLayer> editor;
    std::unique_ptr<RenderTarget> target;
    std::unique_ptr<GraphicsContext> graphicsContext;
};
