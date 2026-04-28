#include "editor/import/ImportSystem.hpp"

#include <iostream>
#include <random>

#include "editor/meta/MetaSerializer.hpp"

namespace tryeditor {

AssetContext ImportSystem::ResolveContext(const std::filesystem::path& asset_path) const {
    AssetContext ctx;
    ctx.asset_path = asset_path;
    ctx.meta_path = asset_path.string() + ".meta";

    // Определяем, где мы находимся
    std::string path_str = asset_path.string();
    if (path_str.find("engine_content") != std::string::npos) {
        ctx.project_assets_dir = engine_assets_dir_;
        ctx.artifacts_dir = engine_artifacts_dir_;
        ctx.cache_dir = engine_cache_dir_;
    } else {
        ctx.project_assets_dir = game_assets_dir_;
        ctx.artifacts_dir = game_artifacts_dir_;
        ctx.cache_dir = game_cache_dir_;
    }
    return ctx;
}

bool ImportSystem::ValidateArtifacts(uint64_t guid, const std::filesystem::path& artifacts_dir) const {
    std::filesystem::path folder = artifacts_dir / std::to_string(guid);
    if (!std::filesystem::exists(folder) || !std::filesystem::is_directory(folder)) {
        return false;
    }
    // Проверяем, что папка не пуста
    return !std::filesystem::is_empty(folder);
}

void ImportSystem::Refresh() {
    auto ensure_dirs = [](const std::filesystem::path& as, const std::filesystem::path& ar, const std::filesystem::path& ca) {
        if (!std::filesystem::exists(as)) std::filesystem::create_directories(as);
        if (!std::filesystem::exists(ar)) std::filesystem::create_directories(ar);
        if (!std::filesystem::exists(ca)) std::filesystem::create_directories(ca);
    };

    ensure_dirs(game_assets_dir_, game_artifacts_dir_, game_cache_dir_);
    ensure_dirs(engine_assets_dir_, engine_artifacts_dir_, engine_cache_dir_);

    id_to_path_.clear();
    path_to_id_.clear();

    ProcessDirectory(game_assets_dir_);
    ProcessDirectory(engine_assets_dir_);
}

void ImportSystem::ProcessDirectory(const std::filesystem::path& assets_dir) {
    if (!std::filesystem::exists(assets_dir)) return;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(assets_dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() == ".meta") continue;

        std::filesystem::path asset_path = entry.path();
        AssetContext ctx = ResolveContext(asset_path);

        if (std::filesystem::exists(ctx.meta_path)) {
            auto header_opt = MetaSerializer::ReadHeader(ctx.meta_path);
            if (header_opt) {
                uint64_t guid = header_opt->guid;
                std::string importer_type = header_opt->importer_type;

                // Регистрируем путь в мапах
                std::string relative_path = std::filesystem::relative(asset_path, root_path_).string();
                id_to_path_[guid] = relative_path;
                path_to_id_[relative_path] = guid;

                auto it = importers_by_name_.find(importer_type);
                if (it != importers_by_name_.end()) {
                    if (!ValidateArtifacts(guid, ctx.artifacts_dir)) {
                        ReimportAsset(*header_opt);
                    }
                }
            }
        } else {
            std::string ext = asset_path.extension().string();
            auto it = importers_by_ext_.find(ext);
            if (it != importers_by_ext_.end()) {
                ImportNewAsset(ctx, it->second);
            }
        }
    }
}

void ImportSystem::ImportNewAsset(const AssetContext& ctx, IAssetImporter* importer) {
    if (!importer) return;

    // Генерация GUID
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    uint64_t new_guid = gen();

    if (importer->ImportNew(ctx, new_guid)) {
        std::string relative_path = std::filesystem::relative(ctx.asset_path, root_path_).string();
        id_to_path_[new_guid] = relative_path;
        path_to_id_[relative_path] = new_guid;
        std::cout << "[ImportSystem] Импортирован новый ассет: " << relative_path << " (GUID: " << new_guid << ")\n";
    }
}

// void ImportSystem::ReimportAsset(const AssetContext& ctx, IAssetImporter* importer) {
//     if (!importer) return;
//
//     if (importer->Reimport(ctx)) {
//         std::cout << "[ImportSystem] Пересобран артефакт для: " << ctx.asset_path.filename() << "\n";
//     }
// }
void ImportSystem::ReimportAsset(const AssetMetaHeader& header) {
    const auto importer = GetImporterByName(header.importer_type);
    const auto path = GetPath(header.guid);

    auto ctx = ResolveContext(path);

    if (importer->Reimport(ctx)) {
        std::cout << "[ImportSystem] Пересобран артефакт для: " << ctx.asset_path.filename() << "\n";
    }
}

void ImportSystem::DeleteArtifactsAndCache(uint64_t id) {
    std::vector<std::filesystem::path> possible_dirs = {
        game_artifacts_dir_ / std::to_string(id),
        game_cache_dir_ / std::to_string(id),
        engine_artifacts_dir_ / std::to_string(id),
        engine_cache_dir_ / std::to_string(id)
    };

    for (const auto& dir : possible_dirs) {
        if (std::filesystem::exists(dir)) {
            std::filesystem::remove_all(dir);
        }
    }
}

void ImportSystem::DeleteAsset(const std::filesystem::path& asset_path) {
    std::filesystem::path absolute_path = std::filesystem::absolute(asset_path);
    if (!std::filesystem::exists(absolute_path)) return;

    std::filesystem::path meta_path = absolute_path.string() + ".meta";
    auto header_opt = MetaSerializer::ReadHeader(meta_path);

    if (header_opt) {
        const auto& header = header_opt.value();
        for (uint64_t sub_id : header.sub_assets) {
            DeleteArtifactsAndCache(sub_id);
        }
        DeleteArtifactsAndCache(header.guid);
    }

    std::error_code ec;
    std::filesystem::remove(absolute_path, ec);
    if (std::filesystem::exists(meta_path)) {
        std::filesystem::remove(meta_path, ec);
    }

    std::filesystem::path parent = absolute_path.parent_path();
    while ((parent != game_assets_dir_ && parent != engine_assets_dir_) &&
           std::filesystem::exists(parent) && std::filesystem::is_empty(parent)) {
        std::filesystem::remove(parent, ec);
        parent = parent.parent_path();
    }
}

void ImportSystem::DeleteDirectory(const std::filesystem::path& dir_path) {
    if (!std::filesystem::exists(dir_path)) return;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir_path)) {
        if (entry.is_regular_file() && entry.path().extension() != ".meta") {
            DeleteAsset(entry.path());
        }
    }
    std::error_code ec;
    std::filesystem::remove_all(dir_path, ec);
}

}  // namespace tryeditor