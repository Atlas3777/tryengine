#pragma once
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <fstream>
#include <random>

#include "editor/asset_factories/IAssetFactory.hpp"
#include "engine/graphics/MaterialSystem.hpp"

namespace tryeditor {

class ShaderAssetFactory : public IAssetFactory {
public:
    // 1. Для GUI (вызывается из FileBrowser)
    uint64_t CreateDefault(const std::filesystem::path& directory) override {
        // Создаем с дефолтными параметрами
        tryengine::graphics::ShaderAsset default_def;
        return Create(directory, "NewShader.shader", default_def);
    }

    uint64_t Create(const std::filesystem::path& directory, const std::string& name,
                    const tryengine::graphics::ShaderAsset& def) {
        const std::filesystem::path asset_path = directory / name;

        // Сохраняем ИСХОДНЫЙ файл (JSON)
        {
            std::ofstream os(asset_path);
            cereal::JSONOutputArchive archive(os);
            archive(cereal::make_nvp("data", def));
        }

        uint64_t guid = CreateMeta(asset_path);
        CreateArtefact(guid, def);

        return guid;
    }

    [[nodiscard]] std::string GetActionName() const override { return "Shader Asset"; }

private:
    uint64_t CreateMeta(const std::filesystem::path& asset_path) {
        std::filesystem::path meta_path = asset_path.string() + ".meta";  // myshader.shader.meta

        std::random_device rd;
        std::mt19937_64 id(rd());
        const uint64_t asset_id = id();

        AssetMetaHeader asset_header;
        asset_header.guid = asset_id;
        asset_header.importer_type = "native";
        asset_header.asset_type = "shader";

        // 3. Сохраняем МЕТА-ФАЙЛ (JSON)
        {
            std::ofstream os(meta_path);
            cereal::JSONOutputArchive archive(os);
            archive(cereal::make_nvp("header", asset_header));
        }
        return asset_id;
    }

    void CreateArtefact(uint64_t id, const tryengine::graphics::ShaderAsset& def) {
        std::filesystem::path folder = artefacts_dir_ / std::to_string(id);
        std::filesystem::create_directories(folder);

        std::filesystem::path path = folder / (std::to_string(id) + ".shd");

        std::ofstream os(path, std::ios::binary);
        cereal::BinaryOutputArchive archive(os);
        archive(def);
    }
    std::filesystem::path artefacts_dir_ = std::filesystem::current_path() / "game" / "artefacts";
};

}  // namespace tryeditor