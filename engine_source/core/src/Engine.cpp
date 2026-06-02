#include "engine/core/Engine.hpp"

#include "engine/core/Addressables.hpp"
#include "engine/core/Clock.hpp"
#include "engine/core/ComponentRegistry.hpp"
#include "engine/core/ResourceManager.hpp"
#include "engine/core/SceneManager.hpp"

#include "engine/core/ScriptSystem.hpp"

namespace tryengine::core {
Engine::Engine() {
    clock = std::make_unique<Clock>();
    resource_manager_ = std::make_unique<ResourceManager>();
    component_registry_ = std::make_unique<ComponentRegistry>();
    scene_manager_ = std::make_unique<SceneManager>(*component_registry_, *resource_manager_);
    script_system_ = std::make_unique<ScriptSystem>();
}

Engine::~Engine() = default;

void Engine::UpdateTime() const {
    clock->Update();
}

}  // namespace tryengine::core
