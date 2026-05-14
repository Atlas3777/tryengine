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

class SceneAssetFactory : public IAssetFactory {
public:
    explicit SceneAssetFactory(ImportSystem& import_system, tryengine::core::ComponentRegistry& registry)
        : import_system_(import_system), component_registry_(registry) {}

    [[nodiscard]] std::string GetActionName() const override { return "Scene Asset"; }

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

        const auto editor_camera = registry.create();
        registry.emplace<tryengine::Tag>(editor_camera, "EditorCamera");
        registry.emplace<tryengine::Transform>(
            editor_camera, tryengine::Transform{glm::vec3(0.f, 0.f, 10.f), glm::quat(), glm::vec3(1.f)});
        registry.emplace<tryengine::Camera>(editor_camera);
        registry.emplace<EditorCameraTag>(editor_camera);
        registry.emplace<tryengine::Relationship>(editor_camera);

        return Create(directory, "NewScene.scene", scene);
    }

    uint64_t Create(const std::filesystem::path& directory, const std::string& name, tryengine::core::Scene& scene) {
        std::filesystem::path asset_path = directory / name;

        if (asset_path.extension() != ".scene") {
            asset_path += ".scene";
        }

        AssetMetaHeader header = CreateMeta(asset_path);
        Save(scene, asset_path, header);

        return header.guid;
    }

    void Save(tryengine::core::Scene& scene, const std::filesystem::path& asset_path, AssetMetaHeader header) {
        std::ofstream os(asset_path);
        if (!os.is_open()) {
            std::cerr << "Error: Failed to open file stream: " << asset_path << std::endl;
        }

        cereal::JSONOutputArchive archive(os);
        component_registry_.Serialize(scene.GetRegistry(), archive);

        {
            auto context = import_system_.ResolveContext(asset_path);
            std::string asset_guid = std::to_string(header.guid);
            std::filesystem::path artifact_dir = context.artifacts_dir / asset_guid;
            std::filesystem::create_directories(artifact_dir);

            std::ofstream os(artifact_dir / asset_guid, std::ios::binary);
            if (os.is_open()) {
                cereal::BinaryOutputArchive archive(os);
                component_registry_.Serialize(scene.GetRegistry(), archive);
            } else
                std::cerr << "[SceneManager]: Serialization error: " << asset_guid << std::endl;
        }
    }

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
