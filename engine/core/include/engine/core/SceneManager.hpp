#pragma once

#include <memory>
#include <string>

#include "Scene.hpp"

namespace engine::core {

class SceneManager {
public:
    SceneManager() = default;
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    SceneManager(SceneManager&&) = default;
    SceneManager& operator=(SceneManager&&) = default;

    ~SceneManager() = default;

    Scene* CreateScene(const std::string& name);

    bool LoadScene(const std::string& filepath);
    bool SaveScene(const std::string& filepath);

    Scene* GetActiveScene() { return active_scene_.get(); }

private:
    std::unique_ptr<Scene> active_scene_ = nullptr;
};

}  // namespace engine::core