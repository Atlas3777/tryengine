#pragma once

#include "asset_factories/SceneAssetFactory.hpp"
#include "editor/AddressablesProvider.hpp"
#include "editor/import/ImportSystem.hpp"
#include "engine/core/SceneManager.hpp"
#include "utils/Paths.hpp"

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
        const auto dir = Paths::assets / "scenes";
        std::filesystem::create_directory(dir);

        auto id = scene_asset_factory_.Create(scene, scene.GetName(), dir);
        scene.SetAssetID(id);

        addressables_provider_.AddAssetInGroup("scenes", scene.GetName() + std::to_string(id), id);
        return true;
    }

    bool OverwriteExistingScene(tryengine::core::Scene& scene) {
        auto id = scene.GetAssetID();
        auto path = import_system_.GetPath(id);

        scene_asset_factory_.SaveSource(path, scene);
        return true;
    }

private:
    tryengine::core::SceneManager& scene_manager_;
    ImportSystem& import_system_;
    SceneAssetFactory& scene_asset_factory_;
    AddressablesProvider& addressables_provider_;
};
}  // namespace tryeditor
