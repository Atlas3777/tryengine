#pragma once
#include <cereal/types/string.hpp>
#include <filesystem>
#include <fstream>
#include <string>

#include "editor/asset_factories/IAssetFactory.hpp"
#include "editor/import/ImportSystem.hpp"
#include "editor/meta/MetaSerializer.hpp"
#include "engine/core/ComponentRegistry.hpp"
#include "engine/core/Components.hpp"
#include "engine/core/RandomUtil.hpp"
#include "engine/core/Scene.hpp"

namespace tryeditor {

struct SceneAssetProxy {
    tryengine::core::Scene& scene;
    tryengine::core::ComponentRegistry& registry;

    template <class Archive>
    void save(Archive& ar) const {
        registry.Serialize(scene.GetRegistry(), ar);
    }

    template <class Archive>
    void load(Archive& ar) {
        registry.Deserialize(scene.GetRegistry(), ar);
    }
};

class SceneAssetFactory : public IAssetFactory {
public:
    explicit SceneAssetFactory(ImportSystem& import_system, tryengine::core::ComponentRegistry& registry)
        : import_system_(import_system), component_registry_(registry) {}

    uint64_t CreateDefault(const std::filesystem::path& directory) override {
        tryengine::core::Scene scene("Some Scene Name");

        auto& registry = scene.GetRegistry();

        const auto game_camera = registry.create();
        registry.emplace<tryengine::Tag>(game_camera, "GameCamera");
        registry.emplace<tryengine::Transform>(
            game_camera, tryengine::Transform{glm::vec3(0.f, 2.f, 10.f), glm::quat(), glm::vec3(1.f)});
        registry.emplace<tryengine::Camera>(game_camera);
        registry.emplace<tryengine::MainCameraTag>(game_camera);
        registry.emplace<tryengine::Relationship>(game_camera);

        return Create(directory, "NewScene.scene", scene);
    }

    uint64_t Create(const std::filesystem::path& directory, const std::string& name, tryengine::core::Scene& scene) {
        const std::filesystem::path asset_path = directory / name;
        AssetMetaHeader header = CreateMeta(asset_path);

        SceneAssetProxy proxy{scene, component_registry_};

        import_system_.SaveNativeAsset<SceneAssetProxy>(asset_path, proxy, header);

        return header.guid;
    }

    [[nodiscard]] std::string GetActionName() const override { return "Scene Asset"; }

private:
    AssetMetaHeader CreateMeta(const std::filesystem::path& asset_path) {
        std::filesystem::path meta_path = asset_path.string() + ".meta";

        AssetMetaHeader header;
        header.guid = tryengine::core::RandomUtil::GenerateInt64();
        header.importer_type = "native";
        header.asset_type = "scene";

        MetaSerializer::WriteHeaderOnly(meta_path, header);

        return header;
    }

    ImportSystem& import_system_;
    tryengine::core::ComponentRegistry& component_registry_;
};

}  // namespace tryeditor