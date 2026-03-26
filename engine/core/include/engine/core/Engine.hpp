#pragma once
#include <mutex>
#include <vector>

#include "engine/core/Clock.hpp"
#include "engine/core/EngineCommands.hpp"
#include "engine/core/InputTypes.hpp"
#include "engine/core/SceneManager.hpp"

namespace engine::core {
class Engine {
public:
    Engine();
    void UpdateTime() const;
    void DispatchCommands();
    void PushCommand(const EngineCommand& cmd);

    void SetInputSource(InputState* source) { input = source; }

    [[nodiscard]] SceneManager& GetSceneManager() const { return *sceneManager; }
    [[nodiscard]] Clock& GetClock() const { return *clock; }
    [[nodiscard]] InputState& GetInput() const { return *input; }

private:
    std::unique_ptr<SceneManager> sceneManager;
    std::unique_ptr<Clock> clock;

    InputState* input = nullptr;

    std::vector<EngineCommand> commandQueue;
    std::mutex commandMutex;
};
}  // namespace engine::core
