#include "engine/core/Clock.hpp"
#include "engine/core/Engine.hpp"

namespace tryengine::core {
Engine::Engine() {
    scene_manager_ = std::make_unique<SceneManager>();
    clock = std::make_unique<Clock>();
    resource_manager_ = std::make_unique<ResourceManager>();
}

void Engine::UpdateTime() const {
    clock->Update();
}

}  // namespace tryengine::core
