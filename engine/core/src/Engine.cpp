#include "engine/core/Engine.hpp"

#include "engine/core/Clock.hpp"

namespace engine::core {
Engine::Engine() {
    scene_manager_ = std::make_unique<SceneManager>();
    clock = std::make_unique<Clock>();
    resource_manager_ = std::make_unique<ResourceManager>();
}

void Engine::UpdateTime() const {
    clock->Update();
}

}  // namespace engine::core
