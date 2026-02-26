#pragma once
#include <mutex>
#include <vector>

#include "EditorLayer.hpp"
#include "EngineCommands.hpp"
#include "EngineConfig.hpp"
#include "EngineState.hpp"
#include "RenderTarget.hpp"

class Engine {
   public:
    Engine(EngineConfig config);
    void MountHardware();

    void ProcessInput();
    void UpdateTime();
    void DispatchCommands();

    // Рендер оставляем как есть
    using RenderCallback = std::function<void(SDL_GPUCommandBuffer* cmd, RenderTarget* target)>;
    void Render(entt::registry& reg, const RenderCallback& userRenderFunc);

    void PushCommand(const EngineCommand& cmd);

    GraphicsContext& GetGraphicsContext() const { return *graphicsContext.get(); }
    EngineSettings settings;
    TimeState time;
    InputState input;
    bool isRunning = true;

   private:
    std::unique_ptr<GraphicsContext> graphicsContext;
    std::unique_ptr<EditorLayer> editor;
    std::unique_ptr<RenderTarget> target;
    std::vector<EngineCommand> commandQueue;
    std::mutex commandMutex;
};
