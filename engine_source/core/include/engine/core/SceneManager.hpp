#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "engine/core/Engine.hpp"
#include "engine/core/Scene.hpp"

namespace tryengine::core {

class SceneManager {
public:
    SceneManager(Engine& engine) : engine_(engine) {};
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;
    SceneManager(SceneManager&&) = default;
    SceneManager& operator=(SceneManager&&) = default;

    ~SceneManager() = default;

    bool LoadScene(const std::string& scene_name);
    bool SaveCurrentScene();

    [[nodiscard]] Scene& GetActiveScene() const { return *active_scene_; }

private:
    std::filesystem::path current_scene_path_;
    std::unique_ptr<Scene> active_scene_ = nullptr;
    Engine& engine_;
};

}  // namespace tryengine::core