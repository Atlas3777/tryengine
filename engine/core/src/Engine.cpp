#include "engine/core/Engine.hpp"

#include "engine/core/Clock.hpp"

namespace engine::core {
Engine::Engine() {
    sceneManager = std::make_unique<SceneManager>();
    clock = std::make_unique<Clock>();
}

void Engine::UpdateTime() const {
    clock->Update();
}
void Engine::DispatchCommands() {
    // TODO: Реализовать обработку очереди команд
}

void Engine::PushCommand(const EngineCommand& cmd) {
    std::lock_guard<std::mutex> lock(commandMutex);
    commandQueue.push_back(cmd);
}

}  // namespace engine::core
