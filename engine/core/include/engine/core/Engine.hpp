#pragma once
#include <mutex>
#include <vector>

#include "Clock.hpp"
#include "EngineState.hpp"
#include "Input.hpp"
#include "SceneManager.hpp"
#include "engine/core/EngineCommands.hpp"

namespace engine::core {
class Engine {
   public:
    Engine();
    void ProcessInput();
    void UpdateTime();
    void DispatchCommands();

    void PushCommand(const EngineCommand& cmd);

    SceneManager& GetSceneManager() const { return *sceneManager; }
    Clock& GetClock() const { return *clock; }
    EngineSettings settings;
    InputState input;
    bool isRunning = true;

   private:
    std::unique_ptr<SceneManager> sceneManager;
    std::unique_ptr<Clock> clock;
    std::vector<EngineCommand> commandQueue;
    std::mutex commandMutex;
};
}  // namespace engine
