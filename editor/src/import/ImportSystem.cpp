#include "editor/import/ImportSystem.hpp"

#include <cereal/archives/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "editor/meta/AssetHeader.hpp"    // Убедись, что путь правильный
#include "editor/meta/ModelAssetMap.hpp"  // Путь к твоему файлу со структурой ModelAssetMap

namespace tryeditor {

void ImportSystem::Refresh() {
    if (!std::filesystem::exists(assets_directory_)) {
        std::filesystem::create_directories(assets_directory_);
        return;
    }

    // Создаем базовые директории, если их нет
    if (!std::filesystem::exists(artefacts_directory_))
        std::filesystem::create_directories(artefacts_directory_);
    if (!std::filesystem::exists(cache_directory_))
        std::filesystem::create_directories(cache_directory_);

    id_to_path_.clear();
    path_to_id_.clear();

    for (const auto& entry : std::filesystem::recursive_directory_iterator(assets_directory_)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const auto& assetPath = entry.path();
        std::string extension = assetPath.extension().string();

        auto it = importers_.find(extension);
        if (it != importers_.end()) {
            IAssetImporter* importer = it->second.get();

            std::filesystem::path metaPath = assetPath.string() + ".meta";

            if (!std::filesystem::exists(metaPath)) {
                // 1. Генерируем мета-файл (в папке assets рядом с ассетом)
                uint64_t uuid = importer->GenerateMeta(assetPath, metaPath);

                std::filesystem::path artifactDir = artefacts_directory_ / std::to_string(uuid);
                std::filesystem::path cacheDir = cache_directory_ / std::to_string(uuid);

                if (!std::filesystem::exists(artifactDir))
                    std::filesystem::create_directories(artifactDir);


                // 2. Генерируем артефакты и кэш
                importer->GenerateArtifact(assetPath, metaPath, artifactDir, cacheDir, assets_directory_);
            }

            try {
                AssetHeader header = importer->ReadIdentification(metaPath);

                // Записываем путь к основному исходнику (game/assets/lance.glb)
                std::string relativeAssetPath =
                    std::filesystem::relative(assetPath, std::filesystem::current_path()).string();
                id_to_path_[header.main_uuid] = relativeAssetPath;
                path_to_id_[relativeAssetPath] = header.main_uuid;

                // Читаем кэш для Asset Browser'a
                std::filesystem::path artifactDir = artefacts_directory_ / std::to_string(header.main_uuid);
                std::filesystem::path cacheDir = cache_directory_ / std::to_string(header.main_uuid);
                std::filesystem::path cacheFilePath = cacheDir / "hierarchy.json";

                if (std::filesystem::exists(cacheFilePath)) {
                    std::ifstream map_file(cacheFilePath);
                    if (map_file.is_open()) {
                        cereal::JSONInputArchive archive(map_file);
                        ModelAssetMap asset_map;
                        archive(cereal::make_nvp("asset_map", asset_map));

                        // Мапим саб-ассеты (game/artefacts/{main_guid}/{sub_guid}.bin)
                        for (const auto& [sub_id, sub_filename] : asset_map.sub_assets) {
                            std::filesystem::path subAssetAbsPath = artifactDir / sub_filename;
                            std::string subAssetRelPath =
                                std::filesystem::relative(subAssetAbsPath, std::filesystem::current_path()).string();

                            id_to_path_[sub_id] = subAssetRelPath;
                            path_to_id_[subAssetRelPath] = sub_id;
                        }
                    }
                }

            } catch (const std::exception& e) {
                std::cerr << "Failed to read meta/cache for: " << assetPath << " Error: " << e.what() << std::endl;
            }
        }
    }
}

}  // namespace tryeditor