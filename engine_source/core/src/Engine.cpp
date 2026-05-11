#include "engine/core/Engine.hpp"

#include "engine/core/Clock.hpp"
#include "engine/core/ComponentRegistry.hpp"
#include "engine/core/ResourceManager.hpp"
#include "engine/core/SceneManager.hpp"
#include "engine/core/Addressables.hpp"

namespace tryengine::core {
Engine::Engine() {
    clock = std::make_unique<Clock>();
    scene_manager_ = std::make_unique<SceneManager>(*this);
    resource_manager_ = std::make_unique<ResourceManager>();
    component_registry_ = std::make_unique<ComponentRegistry>();
}

Engine::~Engine() = default;

void Engine::UpdateTime() const {
    clock->Update();
}

}  // namespace tryengine::core
