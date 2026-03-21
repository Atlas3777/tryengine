#include "engine/core/Engine.hpp"
#include "engine/core/Clock.hpp"

namespace engine::core {
Engine::Engine() {

}

void Engine::UpdateTime() {
    clock->Update();
}

void Engine::PushCommand(const EngineCommand& cmd) {
    std::lock_guard<std::mutex> lock(commandMutex);
    commandQueue.push_back(cmd);
}


}  // namespace engine
