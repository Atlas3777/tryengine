#pragma once
#include <filesystem>
#include <fstream>
#include <string>

#include "editor/asset_factories/IAssetFactory.hpp"
#include "editor/import/ImportSystem.hpp"
#include "engine/core/ComponentRegistry.hpp"
#include "engine/core/Components.hpp"
#include "engine/core/RandomUtil.hpp"
#include "engine/core/Scene.hpp"

namespace tryeditor {

class SceneAssetFactory : public BaseAssetFactory<tryengine::core::Scene> {
public:
    explicit SceneAssetFactory(ImportSystem& import_system, tryengine::core::ComponentRegistry& registry)
        : import_system_(import_system), component_registry_(registry) {}

    [[nodiscard]] std::string GetAssetType() const override { return "scene"; }
    [[nodiscard]] std::string GetActionName() const override { return "Scene Asset"; }
    [[nodiscard]] std::string GetExtension() const override { return ".scene"; }
    [[nodiscard]] std::string GetDefaultName() const override { return "new_scene"; };
    [[nodiscard]] tryengine::core::Scene CreateDefaultAsset() const override {
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
        return scene;
    }

    void SaveSource(const std::filesystem::path& path, const tryengine::core::Scene& scene) override {
        std::ofstream os(path);
        if (!os.is_open()) {
            std::cerr << "Error: Failed to open file stream: " << path << std::endl;
        }

        cereal::JSONOutputArchive archive(os);
        component_registry_.Serialize(scene.GetRegistry(), archive);
    }

    bool LoadSource(const std::filesystem::path& path, tryengine::core::Scene& out_asset) override {
        std::ifstream is(path);
        if (!is.is_open())
            return false;
        cereal::JSONInputArchive ar(is);
        component_registry_.Deserialize(out_asset.GetRegistry(), ar);
        return true;
    }

    bool SaveArtifact(const std::filesystem::path& path, const tryengine::core::Scene& scene) override {
        std::ofstream os(path, std::ios::binary);
        if (os.is_open()) {
            cereal::BinaryOutputArchive archive(os);
            component_registry_.Serialize(scene.GetRegistry(), archive);
            return true;
        } else
            std::cerr << "[SceneManager]: Serialization error: BinaryOutputArchive" << std::endl;
        return false;
    }

    ImportSystem& import_system_;
    tryengine::core::ComponentRegistry& component_registry_;
};

}  // namespace tryeditor
