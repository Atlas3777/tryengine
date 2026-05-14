#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "engine/core/Engine.hpp"
#include "engine/core/Scene.hpp"

namespace tryengine::core {

class SceneManager {
public:
    SceneManager(ComponentRegistry& component_registry, ResourceManager& resource_manager)
        : component_registry_(component_registry), resource_manager_(resource_manager) {};
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    SceneManager(SceneManager&&) = delete;
    SceneManager& operator=(SceneManager&&) = delete;

    ~SceneManager() = default;

    bool LoadScene(const std::string& scene_name);
    void SetActiveScene(std::unique_ptr<Scene> scene) {
        if (!scene) return;
        active_scene_ = std::move(scene);
    }

    [[nodiscard]] Scene& GetActiveScene() const { return *active_scene_; }
    [[nodiscard]] ComponentRegistry& GetComponentRegistry() const { return component_registry_; }

private:
    std::unique_ptr<Scene> active_scene_ = nullptr;
    ComponentRegistry& component_registry_;
    ResourceManager& resource_manager_;
};

}  // namespace tryengine::core