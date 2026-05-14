#include "engine/core/SceneManager.hpp"

#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "engine/core/ComponentRegistry.hpp"
#include "engine/core/ResourceManager.hpp"

namespace tryengine::core {

bool SceneManager::LoadScene(const uint64_t scene_id) {
    auto path = resource_manager_.GetAssetDatabase().GetPath(scene_id);

    if (!std::filesystem::exists(path)) {
        std::cerr << "Error: File does not exist at path: " << path << std::endl;
        return false;
    }

    std::ifstream is(path);
    if (!is.is_open()) {
        std::cerr << "Error: Failed to open file stream: " << path << std::endl;
        return false;
    }

    auto new_scene = std::make_unique<Scene>();
    new_scene->SetAssetID(scene_id);

    cereal::BinaryInputArchive archive(is);
    component_registry_.Deserialize(new_scene->GetRegistry(), archive);
    component_registry_.ResolveAll(new_scene->GetRegistry(),resource_manager_);

    active_scene_ = std::move(new_scene);
    return true;
}

// Существующая загрузка по имени (Addressables) теперь просто использует метод выше
bool SceneManager::LoadScene(const std::string& scene_name) {
    // Находим GUID по имени в Addressables
    auto id = resource_manager_.GetAddressables().Get(scene_name);

    // Передаем загрузку методу, работающему с GUID
    return LoadScene(id);
}

}  // namespace tryengine::core