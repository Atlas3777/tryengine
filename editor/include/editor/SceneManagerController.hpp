#pragma once

#include "asset_factories/SceneAssetFactory.hpp"
#include "editor/AddressablesProvider.hpp"
#include "editor/Paths.hpp"
#include "editor/import/ImportSystem.hpp"
#include "engine/core/SceneManager.hpp"

namespace tryeditor {
class SceneManagerController {
public:
    SceneManagerController(tryengine::core::SceneManager& scene_manager, ImportSystem& import_system,
                           SceneAssetFactory& scene_asset_factory, AddressablesProvider& addressables_provider)
        : scene_manager_(scene_manager),
          import_system_(import_system),
          scene_asset_factory_(scene_asset_factory),
          addressables_provider_(addressables_provider) {};

    bool SaveScene() {
        auto& scene = scene_manager_.GetActiveScene();

        if (scene.IsPersistent()) {
            return OverwriteExistingScene(scene);
        } else {
            return SaveSceneAs(scene);
        }
    }

    bool SaveSceneAs(tryengine::core::Scene& scene) {
        const auto path = Paths::assets / "scenes";
        std::filesystem::create_directory(path);

        std::string extension = ".scene";
        std::filesystem::path scene_path = path / (scene.GetName() + extension);

        // Логика с инкрементом (scene(1), scene(2)...)
        int counter = 1;
        while (std::filesystem::exists(scene_path)) {
            scene_path = path / (scene.GetName() + "(" + std::to_string(counter) + ")" + extension);
            counter++;
        }

        std::string final_name = scene_path.stem().string();
        auto id = scene_asset_factory_.Create(path, final_name, scene);
        scene.SetAssetID(id);

        addressables_provider_.AddAssetInGroup("scenes", final_name, id);
        return true;
    }

    bool OverwriteExistingScene(tryengine::core::Scene& scene) {
        auto id = scene.GetAssetID();
        auto path = import_system_.GetPath(id);

        scene_asset_factory_.Save(scene, path, id, true);
        return true;
    }

private:
    tryengine::core::SceneManager& scene_manager_;
    ImportSystem& import_system_;
    SceneAssetFactory& scene_asset_factory_;
    AddressablesProvider& addressables_provider_;
};
}  // namespace tryeditor
