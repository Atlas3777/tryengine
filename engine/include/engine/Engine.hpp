#pragma once
#include "SceneManager.hpp"


#include <mutex>
#include <vector>

#include "engine/EngineCommands.hpp"
#include "engine/EngineState.hpp"
#include "engine/GraphicsContext.hpp"
#include "engine/Input.hpp"
#include "engine/RenderTarget.hpp"

namespace engine {
class Engine {
   public:
    // Engine(EngineConfig config);
    void MountHardware();

    void ProcessInput();
    void UpdateTime();
    void DispatchCommands();

    using RenderCallback = std::function<void(SDL_GPUCommandBuffer* cmd, RenderTarget* target)>;
    void Render(entt::registry& reg, const RenderCallback& userRenderFunc);
    void BaseRender();

    void PushCommand(const EngineCommand& cmd);

    GraphicsContext& GetGraphicsContext() const { return *graphicsContext; }
    SceneManager& GetSceneManager() const { return *sceneManager; }
    EngineSettings settings;
    TimeState time;
    InputState input;
    bool isRunning = true;

   private:
    std::unique_ptr<GraphicsContext> graphicsContext;
    std::unique_ptr<RenderTarget> target;
    std::unique_ptr<SceneManager> sceneManager;
    std::vector<EngineCommand> commandQueue;
    std::mutex commandMutex;
};
}  // namespace engine
