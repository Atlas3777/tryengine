#include "engine/core/SceneManager.hpp"

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "engine/core/ComponentRegistry.hpp"

namespace tryengine::core {

bool SceneManager::SaveCurrentScene() {
    if (!active_scene_) return false;

    std::ofstream os(current_scene_path_);
    if (!os.is_open()) return false;

    try {
        cereal::JSONOutputArchive archive(os);
        engine_.GetComponentRegistry().Serialize(active_scene_->GetRegistry(), archive);
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool SceneManager::LoadScene(const std::string& scene_name) {
    auto current_path = std::filesystem::current_path();
    current_scene_path_ = current_path / "game" / "assets" / scene_name;

    // 1. Проверяем, существует ли файл физически
    if (!std::filesystem::exists(current_scene_path_)) {
        std::cerr << "Error: File does not exist at path: " << current_scene_path_ << std::endl;
        // Выведем текущую рабочую директорию для ясности
        std::cerr << "Current working directory was: " << current_path << std::endl;
        return false;
    }

    // 2. Проверяем, не является ли путь директорией
    if (std::filesystem::is_directory(current_scene_path_)) {
        std::cerr << "Error: Path is a directory, not a file: " << current_scene_path_ << std::endl;
        return false;
    }

    std::ifstream is(current_scene_path_);
    if (!is.is_open()) {
        // 3. Если файл есть, но не открывается (например, занят другим процессом или нет прав)
        std::cerr << "Error: Failed to open file stream: " << current_scene_path_ << std::endl;
        return false;
    }

    auto newScene = std::make_unique<Scene>(scene_name);

    try {
        cereal::JSONInputArchive archive(is);
        engine_.GetComponentRegistry().Deserialize(newScene->GetRegistry(), archive);

        active_scene_ = std::move(newScene);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Cereal deserialization error: " << e.what() << std::endl;
        return false;
    }
}

}  // namespace tryengine::core