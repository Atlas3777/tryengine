#include "editor/AssetSourceDatabase.hpp"

#include "editor/JsonParser.hpp"
#include "editor/import/IAssetImporter.hpp"
#include "editor/import/ImportSystem.hpp"
#include "editor/meta/AssetMetaHeader.hpp"
#include "engine/async/GlobalExecutors.hpp"
#include "engine/async/SyncWait.hpp"
#include "engine/async/WhenAll.hpp"
#include "engine/core/FormatUtils.h"
#include "engine/core/RandomUtil.hpp"
#include "engine/resources/ReadFullFile.hpp"

namespace tryeditor {

using namespace tryengine::resources;
using namespace tryengine::async;

const char* meta_ext = ".meta";

eastl::vector<UiFolder>& AssetSourceDatabase::GetFolders(StorageDomain domain) {
    switch (domain) {
        case StorageDomain::Engine:
            return engine_folders_;
        case StorageDomain::Game:
            return game_folders_;
    }
    return engine_folders_;
}

eastl::vector<UiFolder>& AssetSourceDatabase::GetFoldersMutable(StorageDomain domain) {
    switch (domain) {
        case StorageDomain::Engine:
            return engine_folders_;
        case StorageDomain::Game:
            return game_folders_;
    }
    return engine_folders_;
}

AssetSourceDatabase::AssetSourceDatabase(ImportSystem& import_system) : import_system_(import_system) {}

Task<void> AssetSourceDatabase::InitEngineContentSync(AsyncFileManager& file_manager) const {
    AssetMountPoint engine_mount;
    engine_mount.name = "engine";
    engine_mount.assets_dir = "engine_content/assets";
    engine_mount.artifacts_dir = "engine_content/artifacts";

    DomainScanResult scan_data{.mount = engine_mount};
    LoadResourcesAndBuildUi(engine_mount.assets_dir.c_str(), scan_data.assets, scan_data.metas, scan_data.orphan_assets,
                            scan_data.orphan_metas, scan_data.folders);

    TRY_LOG_INFO_CAT(tryengine::core::LogCategory::Importer, "EngineContent Synchronous import started...");

    co_await ProcessDomain(scan_data, file_manager);

    TRY_LOG_INFO_CAT(tryengine::core::LogCategory::Importer, "EngineContent Ready!");

    co_return {};
}

Task<void> AssetSourceDatabase::AsyncLoadGameContent(AsyncFileManager& file_manager) {
    AssetMountPoint game_mount;
    game_mount.name = "game";
    game_mount.assets_dir = "game/assets";
    game_mount.artifacts_dir = "game/artifacts";

    DomainScanResult scan_data{.mount = game_mount};
    LoadResourcesAndBuildUi(game_mount.assets_dir.c_str(), scan_data.assets, scan_data.metas, scan_data.orphan_assets,
                            scan_data.orphan_metas, scan_data.folders);

    TRY_LOG_INFO_CAT(tryengine::core::LogCategory::Importer, "GameContent Async import started...");

    co_await ProcessDomain(scan_data, file_manager);

    file_watcher_.WatchDirrectory(game_mount.assets_dir.c_str());

    TRY_LOG_INFO_CAT(tryengine::core::LogCategory::Importer, "GameContent Ready!");
    co_return {};
}

Task<void> AssetSourceDatabase::ProcessDomain(const DomainScanResult& scan_data, AsyncFileManager& file_manager) const {
    co_await ImportOrphans(scan_data, file_manager);
    co_await ReimportStaleAssets(scan_data, file_manager);
    co_return {};
}

Task<void> AssetSourceDatabase::ImportOrphans(const DomainScanResult& scan_data, AsyncFileManager& file_manager) const {
    const uint32_t count = scan_data.orphan_assets.size();
    if (count == 0)
        co_return {};

    eastl::vector<FileHandle> stat_handles;
    stat_handles.reserve(count);
    for (const auto& path : scan_data.orphan_assets) {
        stat_handles.push_back(file_manager.GetStatAsync(path));
    }

    co_await WaitAllFiles(stat_handles);

    eastl::vector<FileHandle> read_handles;
    read_handles.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint64_t file_size = stat_handles[i].GetFileSize();
        read_handles.push_back(file_manager.ReadChunkAsync(scan_data.orphan_assets[i], 0, file_size));
    }

    co_await WaitAllFiles(read_handles);

    eastl::vector<Task<ImportResult>> import_tasks;
    import_tasks.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        if (!read_handles[i].IsReady())
            continue;

        eastl::string_view asset_path = scan_data.orphan_assets[i];
        const size_t dot_pos = asset_path.rfind('.');

        if (dot_pos == eastl::string_view::npos)
            continue;

        const auto ext = asset_path.substr(dot_pos);

        if (IAssetImporter* importer = import_system_.GetImporterByExtension(ext)) {
            ImportContext ctx(read_handles[i].GetData());
            import_tasks.emplace_back(importer->Import(ctx));
        } else {
            TRY_LOG_WARN_CAT(tryengine::core::LogCategory::Importer, "Не найден импортер для расширения: {}",
                             ext.data());
        }
    }

    if (import_tasks.empty())
        co_return {};

    auto import_results = co_await WhenAll(ThreadPool(), eastl::move(import_tasks));

    if (!import_results.has_value()) {
        TRY_LOG_ERROR("Ошибка выполнения WhenAll: {}", import_results.error().Message());
        co_return Error(import_results.error().Message().data());
    }

    const auto& import_tasks_value = import_results.value();

    eastl::vector<FileHandle> write_handles;

    for (size_t i = 0; i < import_tasks_value.size(); ++i) {
        const auto& import_result = import_tasks_value[i];
        if (!import_result.has_value())
            continue;

        const auto& res = import_result.value();
        for (const auto& artifact : res.artifacts) {
            const char* sub_folder = (artifact.target == ArtifactTarget::Runtime) ? "runtime" : "editor";

            tryengine::fmt::StringFormat path("{}/{}/{}/{}", scan_data.mount.artifacts_dir.c_str(), sub_folder,
                                              res.main_guid, artifact.sub_guid);

            TRY_LOG_INFO("Write Path artifact: {}", path.c_str());
            write_handles.push_back(file_manager.WriteChunkAsyncCopy(path.c_str(), artifact.bytes));
        }

        if (!res.meta_bytes.empty()) {
            tryengine::fmt::StringFormat meta_path("{}{}", scan_data.orphan_assets[i].data(), meta_ext);
            write_handles.push_back(file_manager.WriteChunkAsyncCopy(meta_path.c_str(), res.meta_bytes));
        }
    }

    file_manager.Submit();
    co_await WaitAllFiles(write_handles);

    for (const auto& write_handle : write_handles) {
        if (write_handle.IsFailed()) {
            TRY_LOG_ERROR("Не удалось записать файл на диск: {}", write_handle.GetPath().data());
        }
    }

    co_return {};
}

static eastl::vector<eastl::string> FindAllArtifacts(const AssetMountPoint& mount, const AssetMetaHeader& header) {
    eastl::vector<eastl::string> paths;
    paths.reserve(header.sub_assets.size() + 1);

    eastl::string new_path;
    new_path.reserve(64);

    // Подставляем artifacts_dir из точки монтирования
    std::format_to(std::back_inserter(new_path), "{}/runtime/{}/{}", mount.artifacts_dir.c_str(), header.guid, 0);
    paths.push_back(std::move(new_path));

    for (const auto sub_id : header.sub_assets) {
        auto& path = paths.emplace_back();
        path.reserve(64);
        std::format_to(std::back_inserter(path), "{}/runtime/{}/{}", mount.artifacts_dir.c_str(), header.guid, sub_id);
    }

    return paths;
}

static Task<FileHandle> GetOldestFileTime(AsyncFileManager& file_manager, eastl::vector<eastl::string> artifacts) {
    eastl::vector<FileHandle> sub_asset_stat_handles;
    sub_asset_stat_handles.reserve(artifacts.size());
    for (const auto& artifact : artifacts)
        sub_asset_stat_handles.push_back(file_manager.GetStatAsync(artifact));

    co_await WaitAllFiles(sub_asset_stat_handles);

    uint32_t oldest_index = 0;
    for (uint32_t i = 1; i < sub_asset_stat_handles.size(); ++i) {
        if (sub_asset_stat_handles[i].GetMtime() < sub_asset_stat_handles[oldest_index].GetMtime()) {
            oldest_index = i;
        }
    }

    co_return std::move(sub_asset_stat_handles[oldest_index]);
}

Task<void> AssetSourceDatabase::ReimportStaleAssets(const DomainScanResult& scan_data,
                                                    AsyncFileManager& file_manager) const {
    const uint32_t count = scan_data.assets.size();
    if (count == 0)
        co_return {};

    eastl::vector<FileHandle> asset_stat_handles;
    eastl::vector<FileHandle> meta_stat_handles;
    asset_stat_handles.reserve(count);
    meta_stat_handles.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        asset_stat_handles.push_back(file_manager.GetStatAsync(scan_data.assets[i]));
        meta_stat_handles.push_back(file_manager.GetStatAsync(scan_data.metas[i]));
    }

    co_await WaitAllFiles(asset_stat_handles);
    co_await WaitAllFiles(meta_stat_handles);

    eastl::vector<FileHandle> meta_read_handles;
    meta_read_handles.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        uint64_t meta_size = meta_stat_handles[i].GetFileSize();
        meta_read_handles.push_back(file_manager.ReadChunkAsync(scan_data.metas[i], 0, meta_size));
    }

    co_await WaitAllFiles(meta_read_handles);

    struct HeaderCandidate {
        uint32_t original_index;
        eastl::string importer_type;
    };

    eastl::vector<HeaderCandidate> candidates;
    candidates.reserve(count);

    eastl::vector<Task<FileHandle>> artifact_mtime_tasks;
    artifact_mtime_tasks.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        auto meta_bytes = meta_read_handles[i].GetData();
        auto header = DeserializePartial<HeaderOnly>(meta_bytes);
        if (!header.has_value())
            continue;

        TRY_LOG_TRACE_CAT(tryengine::core::LogCategory::Importer, "reimport candidates: guid - {}",
                          header->header.guid);

        candidates.push_back({i, header->header.importer_type});
        auto artifact_paths = FindAllArtifacts(scan_data.mount, header->header);
        artifact_mtime_tasks.push_back(GetOldestFileTime(file_manager, eastl::move(artifact_paths)));
    }

    auto artifact_results_res = co_await WhenAll(eastl::move(artifact_mtime_tasks));
    if (!artifact_results_res.has_value()) {
        TRY_LOG_ERROR("Ошибка при получении состояния артефактов");
        co_return {};
    }

    const auto& artifact_results = artifact_results_res.value();

    struct StaleItem {
        uint32_t original_index;
        IAssetImporter* importer;
    };

    eastl::vector<StaleItem> stale_items;

    for (size_t k = 0; k < candidates.size(); ++k) {
        if (!artifact_results[k].has_value())
            continue;

        const uint32_t i = candidates[k].original_index;
        const FileTime meta_mtime = meta_stat_handles[i].GetMtime();
        const FileTime asset_mtime = asset_stat_handles[i].GetMtime();
        const FileTime artifact_mtime = artifact_results[k].value().GetMtime();

        if ((meta_mtime > artifact_mtime) || (asset_mtime > artifact_mtime)) {
            if (IAssetImporter* importer = import_system_.GetImporterByName(candidates[k].importer_type)) {
                stale_items.push_back({i, importer});
                TRY_LOG_INFO_CAT(tryengine::core::LogCategory::Importer, "assets NEED reimport: guid - {}",
                                 scan_data.assets[i]);
            }
        } else {
            TRY_LOG_INFO_CAT(tryengine::core::LogCategory::Importer, "assets not need reimport: guid - {}",
                             scan_data.assets[i]);
        }
    }

    if (stale_items.empty())
        co_return {};

    eastl::vector<FileHandle> asset_read_handles;
    asset_read_handles.reserve(stale_items.size());

    for (const auto& item : stale_items) {
        const uint64_t asset_size = asset_stat_handles[item.original_index].GetFileSize();
        asset_read_handles.push_back(file_manager.ReadChunkAsync(scan_data.assets[item.original_index], 0, asset_size));
    }

    co_await WaitAllFiles(asset_read_handles);

    eastl::vector<Task<ImportResult>> import_tasks;
    import_tasks.reserve(stale_items.size());

    for (size_t k = 0; k < stale_items.size(); ++k) {
        const uint32_t i = stale_items[k].original_index;
        ImportContext ctx(asset_read_handles[k].GetData(), meta_read_handles[i].GetData());
        import_tasks.emplace_back(stale_items[k].importer->Import(ctx));
    }

    auto import_results_res = co_await WhenAll(ThreadPool(), eastl::move(import_tasks));
    if (!import_results_res.has_value()) {
        TRY_LOG_ERROR("Ошибка выполнения WhenAll при повторном импорте: {}", import_results_res.error().Message());
        co_return {};
    }

    const auto& import_results = import_results_res.value();

    eastl::vector<FileHandle> write_handles;

    for (size_t k = 0; k < import_results.size(); ++k) {
        const auto& import_result = import_results[k];
        if (!import_result.has_value()) {
            TRY_LOG_ERROR("Ошибка повторного импорта: {}", import_result.error().Message());
            continue;
        }

        const uint32_t i = stale_items[k].original_index;  // Всегда корректный абсолютный индекс!
        const auto& res = import_result.value();

        for (const auto& artifact : res.artifacts) {
            const char* sub_folder = (artifact.target == ArtifactTarget::Runtime) ? "runtime" : "editor";

            tryengine::fmt::StringFormat path("{}/{}/{}/{}", scan_data.mount.artifacts_dir.c_str(), sub_folder,
                                              res.main_guid, artifact.sub_guid);

            write_handles.push_back(file_manager.WriteChunkAsyncCopy(path.c_str(), artifact.bytes));
        }
    }

    file_manager.Submit();
    co_await WaitAllFiles(write_handles);

    for (const auto& write_handle : write_handles) {
        if (write_handle.IsFailed()) {
            TRY_LOG_ERROR("Не удалось записать файл на диск: {}", write_handle.GetPath().data());
        }
    }

    co_return {};
}

}  // namespace tryeditor