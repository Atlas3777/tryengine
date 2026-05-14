#pragma once

#include <memory>
#include <mutex>

namespace tryengine::core {
class Clock;
class SceneManager;
class ComponentRegistry;
class ResourceManager;
struct InputState;
class Engine {
public:
    Engine();
    Engine(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine& operator=(Engine&&) = delete;

    ~Engine();

    void UpdateTime() const;

    void SetInputSource(InputState* source) { input = source; }

    [[nodiscard]] Clock& GetClock() const { return *clock; }
    [[nodiscard]] SceneManager& GetSceneManager() const { return *scene_manager_; }
    [[nodiscard]] InputState& GetInput() const { return *input; }
    [[nodiscard]] ResourceManager& GetResourceManager() const { return *resource_manager_; }
    [[nodiscard]] ComponentRegistry& GetComponentRegistry() const { return *component_registry_; }

private:
    std::unique_ptr<ResourceManager> resource_manager_;
    std::unique_ptr<SceneManager> scene_manager_;
    std::unique_ptr<ComponentRegistry> component_registry_;
    std::unique_ptr<Clock> clock;

    InputState* input = nullptr;
};
}  // namespace tryengine::core
