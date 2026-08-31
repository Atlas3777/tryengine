#include "editor/AssetSourceDatabase.hpp"

#include "editor/JsonParser.hpp"
#include "editor/import/IAssetImporter.hpp"
#include "editor/import/ImportSystem.hpp"
#include "editor/meta/AssetMetaHeader.hpp"
#include "engine/async/GlobalExecutors.hpp"
#include "engine/async/RunAndForget.hpp"
#include "engine/core/FormatUtils.h"
#include "engine/core/MakeError.hpp"
#include "engine/resources/AssetRegistry.hpp"
#include "engine/resources/ReadFullFile.hpp"
#include "EASTL/hash_map.h"

namespace tryeditor {

using namespace tryengine::resources;
using namespace tryengine::async;
using tryengine::core::Error;

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

AssetSourceDatabase::AssetSourceDatabase(ImportSystem& import_system, AssetRegistry& registry)
    : import_system_(import_system), registry_(registry) {}

void AssetSourceDatabase::RegisterArtifactPaths(const AssetMountPoint& mount, uint64_t main_guid,
                                                const eastl::vector<uint64_t>& sub_assets) const {
    LogInfo(LogCategory::Importer, "add to registry: main guid - {}", main_guid);
    if (sub_assets.empty()) {
        registry_.RegisterArtifact(main_guid,
                                   tryengine::fmt::format("{}/runtime/{}/0", mount.artifacts_dir.c_str(), main_guid));
        return;
    }

    for (uint64_t sub_guid : sub_assets) {
        registry_.RegisterArtifact(
            sub_guid, tryengine::fmt::format("{}/runtime/{}/{}", mount.artifacts_dir.c_str(), main_guid, sub_guid));
    }
}

Task<void> AssetSourceDatabase::InitEngineContentSync(AsyncFileManager& file_manager) {
    AssetMountPoint engine_mount{
        .name = "engine", .assets_dir = "engine_content/assets", .artifacts_dir = "engine_content/artifacts"};

    DomainScanResult scan_data{.mount = engine_mount};
    LoadResourcesAndBuildUi(engine_mount.assets_dir.c_str(), scan_data.assets, scan_data.metas, scan_data.orphan_assets,
                            scan_data.orphan_metas, scan_data.folders);

    LogInfo(LogCategory::Importer, "EngineContent processing...");

    co_await ProcessDomain(scan_data, file_manager, StorageDomain::Engine);

    LogInfo(LogCategory::Importer, "EngineContent Ready!");
    co_return {};
}

Task<void> AssetSourceDatabase::AsyncLoadGameContent(AsyncFileManager& file_manager) {
    AssetMountPoint game_mount{.name = "game", .assets_dir = "game/assets", .artifacts_dir = "game/artifacts"};

    DomainScanResult scan_data{.mount = game_mount};
    LoadResourcesAndBuildUi(game_mount.assets_dir.c_str(), scan_data.assets, scan_data.metas, scan_data.orphan_assets,
                            scan_data.orphan_metas, scan_data.folders);

    LogInfo(LogCategory::Importer, "GameContent processing...");

    co_await ProcessDomain(scan_data, file_manager, StorageDomain::Game);

    file_watcher_.WatchDirrectory(game_mount.assets_dir.c_str());

    LogInfo(LogCategory::Importer, "GameContent Ready!");
    co_return {};
}

Task<void> AssetSourceDatabase::ProcessDomain(DomainScanResult& scan_data, AsyncFileManager& file_manager,
                                              StorageDomain domain) {
    // 1. Запуск индивидуальных асинхронных задач для существующих пар (.asset + .meta)
    for (size_t i = 0; i < scan_data.assets.size(); ++i) {
        RunAndForget(ThreadPool(), ProcessExistingAsset(scan_data.assets[i], scan_data.metas[i],
                                                       scan_data.mount, file_manager));
    }

    // 2. Запуск асинхронного импорта сиротских ассетов без мета-файлов
    for (const auto& orphan : scan_data.orphan_assets)
        RunAndForget(ThreadPool(), ImportOrphanAsset(orphan, scan_data.mount, file_manager));

    auto& target_folders = GetFoldersMutable(domain);
    target_folders = eastl::move(scan_data.folders);

    co_return {};
}

Task<void> AssetSourceDatabase::ImportOrphanAsset(eastl::string asset_path, AssetMountPoint mount,
                                                 AsyncFileManager& file_manager) {
    // 1. Получение размера файла
    FileHandle stat_handle = file_manager.GetStatAsyncCopy(asset_path);
    co_await stat_handle;

    if (stat_handle.IsFailed())
        co_return LogAndMakeError("Не удалось получить stat для ассета-сироты: {}", asset_path.c_str());

    // 2. Чтение ассета
    FileHandle read_handle = file_manager.ReadChunkAsyncCopy(asset_path, 0, stat_handle.GetFileSize());
    co_await read_handle;

    if (read_handle.IsFailed()) {
        co_return LogAndMakeError("Не удалось прочитать ассет-сироту: {}", asset_path.c_str());
    }

    // 3. Поиск импортера
    const size_t dot_pos = asset_path.rfind('.');
    if (dot_pos == eastl::string::npos)
        co_return LogAndMakeError("Файл не имеет расширения: {}", asset_path.c_str());


    const auto ext = asset_path.substr(dot_pos);
    IAssetImporter* importer = import_system_.GetImporterByExtension(ext.c_str());
    if (!importer)
        co_return LogAndMakeError("Не найден импортер для расширения: {}", asset_path.c_str());


    // 4. Импорт
    ImportContext ctx(read_handle.GetData());
    auto import_res = co_await importer->Import(ctx);
    if (!import_res.has_value()) {
        LogError(LogCategory::Importer, "Ошибка импорта ассета-сироты {}: {}", asset_path.c_str(),
                 import_res.error().Message());
        co_return eastl::move(import_res.error());
    }

    const auto& res = *import_res;

    // 5. Запись артефактов и мета-файла
    eastl::vector<FileHandle> write_handles;
    eastl::vector<uint64_t> runtime_sub_guids;

    for (const auto& artifact : res.artifacts) {
        const char* sub_folder = (artifact.target == ArtifactTarget::Runtime) ? "runtime" : "editor";
        auto path = tryengine::fmt::format("{}/{}/{}/{}", mount.artifacts_dir.c_str(), sub_folder,
                                           res.main_guid, artifact.sub_guid);

        LogInfo(LogCategory::Importer, "Write Path artifact: {}", path.c_str());
        write_handles.push_back(file_manager.WriteChunkAsyncCopy(path.c_str(), artifact.bytes));

        if (artifact.target == ArtifactTarget::Runtime)
            runtime_sub_guids.push_back(artifact.sub_guid);

    }

    if (!res.meta_bytes.empty()) {
        auto meta_path = tryengine::fmt::format("{}{}", asset_path.c_str(), meta_ext);
        write_handles.push_back(file_manager.WriteChunkAsyncCopy(meta_path.c_str(), res.meta_bytes));
    }

    co_await WaitAllFiles(write_handles);

    for (const auto& write_handle : write_handles) {
        if (write_handle.IsFailed())
            co_return LogAndMakeError("Не удалось записать артефакт: {}", write_handle.GetPath().data());

    }

    // 6. Регистрация артефактов и обновление карт под мьютексом
    RegisterArtifactPaths(mount, res.main_guid, runtime_sub_guids);

    {
        std::lock_guard lock(db_mutex_);
        guid_to_path_[res.main_guid] = asset_path;
        path_to_guid_[asset_path] = res.main_guid;
    }

    LogInfo(LogCategory::Importer, "Ассет-сирота успешно импортирован: {}", asset_path.c_str());
    co_return {};
}

static Task<FileHandle> GetOldestFileTime(AsyncFileManager& file_manager, eastl::vector<eastl::string> artifacts) {
    if (artifacts.empty()) {
        co_return LogAndMakeError("Empty artifact list");
    }

    eastl::vector<FileHandle> stat_handles;
    stat_handles.reserve(artifacts.size());
    for (const auto& artifact : artifacts)
        stat_handles.push_back(file_manager.GetStatAsyncCopy(artifact));

    co_await WaitAllFiles(stat_handles);

    for (auto& handle : stat_handles)
        if (handle.IsFailed())
            co_return std::move(handle);


    uint32_t oldest_index = 0;
    for (uint32_t i = 1; i < stat_handles.size(); ++i)
        if (stat_handles[i].GetMtime() < stat_handles[oldest_index].GetMtime())
            oldest_index = i;


    co_return std::move(stat_handles[oldest_index]);
}

static eastl::vector<eastl::string> FindAllArtifacts(const AssetMountPoint& mount, const AssetMetaHeader& header) {
    eastl::vector<eastl::string> paths;
    paths.reserve(header.sub_assets.size() + 1);

    if (header.sub_assets.empty()) {
        paths.emplace_back(tryengine::fmt::format("{}/runtime/{}/{}", mount.artifacts_dir.c_str(), header.guid, 0));
    } else {
        for (const auto sub_id : header.sub_assets) {
            paths.emplace_back(
                tryengine::fmt::format("{}/runtime/{}/{}", mount.artifacts_dir.c_str(), header.guid, sub_id));
        }
    }

    return paths;
}

Task<void> AssetSourceDatabase::ProcessExistingAsset(eastl::string asset_path, eastl::string meta_path,
                                                   AssetMountPoint mount, AsyncFileManager& file_manager) {
    // 1. Stat ассета и мета-файла
    FileHandle asset_stat = file_manager.GetStatAsyncCopy(asset_path);
    FileHandle meta_stat = file_manager.GetStatAsyncCopy(meta_path);

    co_await WaitAllFiles(asset_stat, meta_stat);

    if (asset_stat.IsFailed() || meta_stat.IsFailed())
        co_return LogAndMakeError("Не удалось получить stat для: {}", asset_path.c_str());

    // 2. Чтение мета-файла
    FileHandle meta_read = file_manager.ReadChunkAsyncCopy(meta_path, 0, meta_stat.GetFileSize());
    co_await meta_read;

    if (meta_read.IsFailed())
        co_return LogAndMakeError("Не удалось прочитать мета-файл: {}", meta_path.c_str());

    auto header_res = DeserializePartial<HeaderOnly>(meta_read.GetData());
    if (!header_res.has_value())
        co_return LogAndMakeError("Ошибка парсинга заголовка мета-файла: {}", meta_path.c_str());

    const auto& header = header_res->header;

    {
        std::lock_guard lock(db_mutex_);
        guid_to_path_[header.guid] = asset_path;
        path_to_guid_[asset_path] = header.guid;
    }

    RegisterArtifactPaths(mount, header.guid, header.sub_assets);

    // 3. Проверка времени модификации артефактов
    auto artifact_paths = FindAllArtifacts(mount, header);

    auto oldest_artifact_stat_res = co_await GetOldestFileTime(file_manager, eastl::move(artifact_paths));

    auto& oldest_artifact_stat = *oldest_artifact_stat_res;

    bool need_reimport = false;
    if (oldest_artifact_stat.IsFailed()) {
        LogInfo(LogCategory::Importer, "NEED reimport (отсутствует артефакт): {}", asset_path.c_str());
        need_reimport = true;
    } else {
        const FileTime meta_mtime = meta_stat.GetMtime();
        const FileTime asset_mtime = asset_stat.GetMtime();
        const FileTime artifact_mtime = oldest_artifact_stat.GetMtime();

        if ((meta_mtime > artifact_mtime) || (asset_mtime > artifact_mtime)) {
            LogInfo(LogCategory::Importer, "NEED reimport: {}", asset_path.c_str());
            need_reimport = true;
        } else {
            LogInfo(LogCategory::Importer, "NOT need reimport: {}", asset_path.c_str());
        }
    }

    if (!need_reimport)
        co_return {};


    // 4. Повторный импорт устаревшего ассета
    IAssetImporter* importer = import_system_.GetImporterByName(header.importer_type.c_str());
    if (!importer)
        co_return LogAndMakeError("Не найден импортер для типа: {}, путь {}", header.importer_type.c_str(), asset_path);

    FileHandle asset_read = file_manager.ReadChunkAsyncCopy(asset_path, 0, asset_stat.GetFileSize());
    co_await asset_read;

    if (asset_read.IsFailed())
        co_return LogAndMakeError("Не удалось прочитать файл ассета для реимпорта: {}", asset_path.c_str());

    ImportContext ctx(asset_read.GetData(), meta_read.GetData());
    auto import_res = co_await importer->Import(ctx);

    if (!import_res.has_value()) {
        LogError(LogCategory::Importer, "Ошибка реимпорта для {}: {}", asset_path.c_str(), import_res.error().Message());
        co_return eastl::move(import_res.error());
    }

    const auto& res = *import_res;
    eastl::vector<FileHandle> write_handles;
    eastl::vector<uint64_t> runtime_sub_guids;

    for (const auto& artifact : res.artifacts) {
        const char* sub_folder = (artifact.target == ArtifactTarget::Runtime) ? "runtime" : "editor";
        auto path = tryengine::fmt::format("{}/{}/{}/{}", mount.artifacts_dir.c_str(), sub_folder,
                                           res.main_guid, artifact.sub_guid);

        write_handles.push_back(file_manager.WriteChunkAsyncCopy(path.c_str(), artifact.bytes));
        if (artifact.target == ArtifactTarget::Runtime) {
            runtime_sub_guids.push_back(artifact.sub_guid);
        }
    }

    co_await WaitAllFiles(write_handles);

    for (const auto& write_handle : write_handles) {
        if (write_handle.IsFailed()) {
            co_return LogAndMakeError("Не удалось записать артефакт при реимпорте: {}",
                                     write_handle.GetPath().data());
        }
    }

    RegisterArtifactPaths(mount, res.main_guid, runtime_sub_guids);
    co_return {};
}

void AssetSourceDatabase::OnFileCreatedOrModified(const char* path) {}

void AssetSourceDatabase::OnFileDeleted(const char* path) {
    std::lock_guard lock(db_mutex_);
    auto it = path_to_guid_.find(eastl::string(path));
    if (it != path_to_guid_.end()) {
        uint64_t guid = it->second;
        guid_to_path_.erase(guid);
        path_to_guid_.erase(it);
    }
}

}  // namespace tryeditor