#pragma once
#include <mutex>

#include "engine/core/Clock.hpp"
#include "engine/core/InputTypes.hpp"
#include "engine/core/ResourceManager.hpp"
#include "engine/core/SceneManager.hpp"

namespace engine::core {
class Engine {
public:
    Engine();
    void UpdateTime() const;

    void SetInputSource(InputState* source) { input = source; }

    [[nodiscard]] SceneManager& GetSceneManager() const { return *scene_manager_; }
    [[nodiscard]] Clock& GetClock() const { return *clock; }
    [[nodiscard]] InputState& GetInput() const { return *input; }
    [[nodiscard]] ResourceManager& GetResourceManager() const { return *resource_manager_; }

private:
    std::unique_ptr<ResourceManager> resource_manager_;
    std::unique_ptr<SceneManager> scene_manager_;
    std::unique_ptr<Clock> clock;

    InputState* input = nullptr;
};
}  // namespace engine::core
