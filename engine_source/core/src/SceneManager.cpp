#include "engine/core/SceneManager.hpp"

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "engine/core/ComponentRegistry.hpp"
#include "engine/core/ResourceManager.hpp"

namespace tryengine::core {

bool SceneManager::LoadScene(const std::string& scene_name) {
    auto id = resource_manager_.GetAddressables().Get(scene_name);
    auto path = resource_manager_.GetAssetDatabase().GetPath(id);
    // auto path = std::filesystem::current_path() / "game" / "artifacts" / "8931749524998491898" / "8931749524998491898";
    // auto path = std::filesystem::current_path() / "game" / "assets" / "folder" / "NewScene.scene";

    if (!std::filesystem::exists(path)) {
        std::cerr << "Error: File does not exist at path: " << path << std::endl;
        std::cerr << "Current working directory was: " << path << std::endl;
        return false;
    }

    std::ifstream is(path);
    if (!is.is_open()) {
        std::cerr << "Error: Failed to open file stream: " << path << std::endl;
        return false;
    }

    auto new_scene = std::make_unique<Scene>(scene_name);

    cereal::BinaryInputArchive archive(is);
    component_registry_.Deserialize(new_scene->GetRegistry(), archive);

    active_scene_ = std::move(new_scene);
    return true;
}

}  // namespace tryengine::core