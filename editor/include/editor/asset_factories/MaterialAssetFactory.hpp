#pragma once
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "editor/asset_factories/IAssetFactory.hpp"
#include "engine/resources/MaterialAssetData.hpp"

namespace tryeditor {

class MaterialAssetFactory : public IAssetFactory {
public:
    // Вызывается из FileBrowser (GUI) с параметрами по умолчанию
    uint64_t CreateDefault(const std::filesystem::path& directory) override {
        tryengine::resources::MaterialAssetData def;
        def.name = "New Material";
        def.shader_asset_id = 0; // ID дефолтного шейдера
        return Create(directory, "NewMaterial.mat", def);
    }

    // Основной метод создания ассета (вызывается из Импортера и других мест)
    uint64_t Create(const std::filesystem::path& directory, const std::string& name,
                    const tryengine::resources::MaterialAssetData& data) {
        
        // Убеждаемся, что расширение присутствует
        std::string filename = name;
        if (filename.find(".mat") == std::string::npos) {
            filename += ".mat";
        }
        
        const std::filesystem::path asset_path = directory / filename;

        // 1. Сохраняем ИСХОДНЫЙ файл (JSON) в папку Assets
        {
            std::ofstream os(asset_path);
            if (os.is_open()) {
                cereal::JSONOutputArchive archive(os);
                archive(cereal::make_nvp("material", data));
            }
        }

        // 2. Создаем Meta-файл и генерируем GUID
        uint64_t guid = CreateMeta(asset_path);

        // 3. Создаем бинарный артефакт (для рантайма) в папке Artefacts
        CreateArtefact(guid, data);

        return guid;
    }

    [[nodiscard]] std::string GetActionName() const override { return "Material Asset"; }

private:
    uint64_t CreateMeta(const std::filesystem::path& asset_path) {
        std::filesystem::path meta_path = asset_path.string() + ".meta";

        // Генерация уникального ID
        std::random_device rd;
        std::mt19937_64 id(rd());
        const uint64_t asset_id = id();

        AssetMetaHeader asset_header;
        asset_header.guid = asset_id;
        asset_header.importer_type = "native";
        asset_header.asset_type = "material";

        // Сохраняем МЕТА-ФАЙЛ (JSON)
        {
            std::ofstream os(meta_path);
            if (os.is_open()) {
                cereal::JSONOutputArchive archive(os);
                archive(cereal::make_nvp("header", asset_header));
            }
        }
        return asset_id;
    }

    void CreateArtefact(uint64_t id, const tryengine::resources::MaterialAssetData& data) {
        std::filesystem::path folder = artefacts_dir_ / std::to_string(id);
        std::filesystem::create_directories(folder);

        // Расширение .matbin, как ожидается вашим движком
        std::filesystem::path path = folder / (std::to_string(id) + ".matbin");

        std::ofstream os(path, std::ios::binary);
        if (os.is_open()) {
            cereal::BinaryOutputArchive archive(os);
            archive(data);
        }
    }

    // Путь до артефактов (аналогично ShaderAssetFactory)
    std::filesystem::path artefacts_dir_ = std::filesystem::current_path() / "game" / "artefacts";
};

}  // namespace tryeditor