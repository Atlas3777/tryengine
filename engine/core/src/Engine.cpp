#include "engine/core/Engine.hpp"
#include "engine/core/Clock.hpp"

namespace engine::core {
Engine::Engine() {
    sceneManager = std::make_unique<SceneManager>();
    clock = std::make_unique<Clock>();
}

void Engine::UpdateTime() {
    clock->Update();
}

void Engine::PushCommand(const EngineCommand& cmd) {
    std::lock_guard<std::mutex> lock(commandMutex);
    commandQueue.push_back(cmd);
}

void Engine::ProcessInput(InputState& input2) {
    input = &input2;
}

}  // namespace engine
