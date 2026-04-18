#include "editor/import/ImportSystem.hpp"

#include <cereal/archives/json.hpp>
#include <fstream>
#include <iostream>

namespace tryeditor {

std::optional<AssetMetaHeader> ImportSystem::PeekMetaHeader(const std::filesystem::path& meta_path) {
    if (!std::filesystem::exists(meta_path))
        return std::nullopt;

    try {
        std::ifstream meta_file(meta_path);
        if (!meta_file.is_open())
            return std::nullopt;

        cereal::JSONInputArchive archive(meta_file);
        AssetMetaHeader header;
        archive(cereal::make_nvp("header", header));
        return header;
    } catch (...) {
        return std::nullopt;
    }
}

void ImportSystem::Refresh() {
    if (!std::filesystem::exists(assets_directory_))
        std::filesystem::create_directories(assets_directory_);
    if (!std::filesystem::exists(artefacts_directory_))
        std::filesystem::create_directories(artefacts_directory_);
    if (!std::filesystem::exists(cache_directory_))
        std::filesystem::create_directories(cache_directory_);

    id_to_path_.clear();
    path_to_id_.clear();

    for (const auto& entry : std::filesystem::recursive_directory_iterator(assets_directory_)) {
        if (entry.is_regular_file() && entry.path().extension() == ".meta") {
            auto header = PeekMetaHeader(entry.path());
            if (header) {
                // Путь ассета — это путь мета-файла без расширения .meta
                std::filesystem::path asset_path = entry.path().parent_path() / entry.path().stem();
                if (std::filesystem::exists(asset_path)) {
                    id_to_path_[header->guid] = asset_path.string();
                    path_to_id_[asset_path.string()] = header->guid;
                }
            }
        }
    }

    // Теперь основной цикл импорта
    for (const auto& entry : std::filesystem::recursive_directory_iterator(assets_directory_)) {
        if (!entry.is_regular_file() || entry.path().extension() == ".meta")
            continue;

        const auto& assetPath = entry.path();
        std::string extension = assetPath.extension().string();
        std::filesystem::path meta_path = assetPath.string() + ".meta";

        IAssetImporter* importer = nullptr;
        std::optional<AssetMetaHeader> header;

        try {
            if (std::filesystem::exists(meta_path)) {
                header = PeekMetaHeader(meta_path);
                if (header) importer = GetImporterByName(header->importer_type);
            } else {
                auto it = importers_by_ext_.find(extension);
                if (it != importers_by_ext_.end()) {
                    importer = it->second;
                    uint64_t guid = importer->GenerateMeta(assetPath, meta_path);

                    // Создаем папки
                    DeleteArtifactsAndCache(guid); // На всякий случай чистим старое
                    std::filesystem::create_directories(artefacts_directory_ / std::to_string(guid));
                    std::filesystem::create_directories(cache_directory_ / std::to_string(guid));

                    importer->GenerateArtifact(assetPath, meta_path, artefacts_directory_ / std::to_string(guid),
                                               cache_directory_ / std::to_string(guid), assets_directory_);

                    header = importer->ReadIdentification(meta_path);
                }
            }

            if (header) {
                std::string relativeAssetPath =
                    std::filesystem::relative(assetPath, std::filesystem::current_path()).string();

                id_to_path_[header->guid] = relativeAssetPath;
                path_to_id_[relativeAssetPath] = header->guid;

                // 4. Специфичные для импортера действия (Иерархия)
                if (importer && importer->HasHierarchy()) {
                    std::filesystem::path cacheDir = cache_directory_ / std::to_string(header->guid);
                    if (!std::filesystem::exists(cacheDir))
                        std::filesystem::create_directories(cacheDir);
                    importer->GenerateHierarchy(assetPath, cacheDir);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Failed to process: " << assetPath << " Error: " << e.what() << std::endl;
        }
    }
}

void ImportSystem::DeleteArtifactsAndCache(uint64_t id) {
    std::filesystem::path artifact_dir = artefacts_directory_ / std::to_string(id);
    std::filesystem::path cache_dir = cache_directory_ / std::to_string(id);

    if (std::filesystem::exists(artifact_dir)) {
        std::filesystem::remove_all(artifact_dir);
    }
    if (std::filesystem::exists(cache_dir)) {
        std::filesystem::remove_all(cache_dir);
    }
}

void ImportSystem::DeleteAsset(const std::filesystem::path& asset_path) {
    // 1. Приводим путь к абсолютному, чтобы не зависеть от current_path
    std::filesystem::path absolute_path = std::filesystem::absolute(asset_path);
    if (!std::filesystem::exists(absolute_path)) return;

    std::filesystem::path meta_path = absolute_path.string() + ".meta";
    auto header_opt = PeekMetaHeader(meta_path);

    if (header_opt) {
        const auto& header = header_opt.value();

        // 2. Рекурсивно удаляем все дочерние ассеты
        for (uint64_t sub_id : header.sub_assets) {
            auto it = id_to_path_.find(sub_id);
            if (it != id_to_path_.end()) {
                // Находим полный путь к дочернему ассету
                std::filesystem::path sub_full_path = std::filesystem::absolute(it->second);

                if (std::filesystem::exists(sub_full_path) && sub_full_path != absolute_path) {
                    DeleteAsset(sub_full_path); // Рекурсия
                }
            } else {
                // Если пути в мапе нет, хотя бы почистим кэш
                DeleteArtifactsAndCache(sub_id);
            }
        }

        // 3. Удаляем кэш и артефакты самого родителя
        DeleteArtifactsAndCache(header.guid);
    }

    // 4. Удаляем физические файлы ассета
    std::error_code ec;
    std::filesystem::remove(absolute_path, ec);
    if (std::filesystem::exists(meta_path)) {
        std::filesystem::remove(meta_path, ec);
    }

    // 5. БОНУС: Рекурсивно удаляем родительские папки, если они стали пустыми
    // (кроме корня assets_directory_)
    std::filesystem::path parent = absolute_path.parent_path();
    while (parent != assets_directory_ && std::filesystem::exists(parent) && std::filesystem::is_empty(parent)) {
        std::filesystem::remove(parent, ec);
        parent = parent.parent_path();
    }
}

void ImportSystem::DeleteDirectory(const std::filesystem::path& dir_path) {
    if (!std::filesystem::exists(dir_path)) return;

    // Сначала проходим по всем файлам (не .meta) и удаляем их через систему ассетов,
    // чтобы подчистить кэши и артефакты.
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir_path)) {
        if (entry.is_regular_file() && entry.path().extension() != ".meta") {
            DeleteAsset(entry.path());
        }
    }

    // Затем удаляем саму папку со всем, что в ней могло остаться
    std::error_code ec;
    std::filesystem::remove_all(dir_path, ec);
}

}  // namespace tryeditor