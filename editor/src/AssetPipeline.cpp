#include "editor/AssetPipeline.hpp"

#include "../../engine_source/resources/include/engine/resources/Json.hpp"
#include "editor/UiTree.hpp"
#include "editor/import/IAssetImporter.hpp"
#include "editor/import/ImportStamp.hpp"
#include "editor/import/ImportSystem.hpp"
#include "editor/meta/AssetMetaHeader.hpp"
#include "engine/async/GlobalExecutors.hpp"
#include "engine/async/RunAndForget.hpp"
#include "engine/async/WhenAll.hpp"
#include "engine/core/FormatUtils.h"
#include "engine/core/MakeError.hpp"
#include "engine/core/strings/Hash.hpp"
#include "engine/resources/AssetRegistry.hpp"
#include "engine/resources/ReadFullFile.hpp"

namespace tryeditor {

using namespace tryengine::resources;
using namespace tryengine::async;
using tryengine::core::Error;
using tryengine::core::PathHash;
using tryengine::core::StringTable;
using tryengine::core::ToView;

const char* meta_ext = ".meta";

const UiTree& AssetPipeline::GetTree(StorageDomain domain) const {
    return domain == StorageDomain::Engine ? engine_tree_ : game_tree_;
}

const eastl::vector<UiFolder>& AssetPipeline::GetFolders(StorageDomain domain) const {
    return GetTree(domain).Folders();
}

AssetPipeline::AssetPipeline(AssetRegistry& registry) : registry_(registry) {}

void AssetPipeline::RegisterArtifactPaths(const AssetMountPoint& mount, uint64_t main_guid,
                                          const eastl::vector<uint64_t>& sub_assets) const {
    LogInfo(LogCategory::Importer, "add to registry: main guid - {}", main_guid);
    if (sub_assets.empty()) {
        registry_.RegisterLooseArtifact(
            main_guid, tryengine::fmt::format("{}/runtime/{}/0", mount.artifacts_dir.c_str(), main_guid));
        return;
    }

    for (uint64_t sub_guid : sub_assets) {
        registry_.RegisterLooseArtifact(
            sub_guid, tryengine::fmt::format("{}/runtime/{}/{}", mount.artifacts_dir.c_str(), main_guid, sub_guid));
    }
}

Task<void> AssetPipeline::ImportEngineContent(AsyncFileManager& file_manager) {
    AssetMountPoint engine_mount{
        .name = "engine", .assets_dir = "engine_content/assets", .artifacts_dir = "engine_content/artifacts"};

    LogInfo(LogCategory::Importer, "EngineContent scanning...");
    ScanResult scan_data = AssetScanner::ScanDirectory(engine_mount.assets_dir.c_str());

    LogInfo(LogCategory::Importer, "EngineContent processing...");
    co_await ProcessDomain(engine_mount, eastl::move(scan_data), file_manager, StorageDomain::Engine);

    LogInfo(LogCategory::Importer, "EngineContent Ready!");
    co_return {};
}

Task<void> AssetPipeline::ImportGameContent(AsyncFileManager& file_manager) {
    AssetMountPoint game_mount{.name = "game", .assets_dir = "game/assets", .artifacts_dir = "game/artifacts"};

    LogInfo(LogCategory::Importer, "GameContent scanning...");
    ScanResult scan_data = AssetScanner::ScanDirectory(game_mount.assets_dir.c_str());

    LogInfo(LogCategory::Importer, "GameContent processing...");
    co_await ProcessDomain(game_mount, eastl::move(scan_data), file_manager, StorageDomain::Game);

    file_watcher_.WatchDirrectory(game_mount.assets_dir.c_str());

    LogInfo(LogCategory::Importer, "GameContent Ready!");
    co_return {};
}

Task<void> AssetPipeline::ProcessDomain(const AssetMountPoint& mount, ScanResult scan_data,
                                        AsyncFileManager& file_manager, StorageDomain domain) {
    eastl::vector<Task<void>> tasks;
    tasks.reserve(scan_data.matched_assets.size() + scan_data.orphan_assets.size());

    // 1. Фаза чтения метафайлов и проверки актуальности существующих ассетов
    for (auto& pair : scan_data.matched_assets)
        tasks.push_back(ProcessExistingAsset(eastl::move(pair), mount, file_manager));

    // 2. Фаза импорта ассетов-сирот (создаются .meta и генерируются валидные GUID)
    for (auto& orphan : scan_data.orphan_assets)
        tasks.push_back(ImportOrphanAsset(eastl::move(orphan), mount, file_manager));

    auto expecteds = co_await WhenAll(ThreadPool(), eastl::move(tasks));

    if (!expecteds.has_value())
        co_return LogAndMakeError("Ошибка при асинхронной обработке ассетов");

    // 3. Фаза построения UI-дерева (все GUID 100% гарантированно зарегистрированы).
    // Первичная загрузка: вызывать, пока das не рисует это дерево (старт редактора).
    {
        std::lock_guard lock(db_mutex_);
        UiTree& tree = (domain == StorageDomain::Engine) ? engine_tree_ : game_tree_;
        tree.BuildFrom(scan_data, path_to_guid_);
    }

    co_return {};
}

namespace {

eastl::string MakeStampPath(const AssetMountPoint& mount, uint64_t guid) {
    return tryengine::fmt::format("{}/stamps/{}.json", mount.artifacts_dir.c_str(), guid);
}

// Путь артефакта относительно artifacts_dir. Один и тот же формат у записи и у штампа.
eastl::string MakeArtifactRelPath(uint64_t main_guid, const ProducedArtifact& artifact) {
    const char* sub_folder = (artifact.target == ArtifactTarget::Runtime) ? "runtime" : "editor";
    return tryengine::fmt::format("{}/{}/{}", sub_folder, main_guid, artifact.sub_guid);
}

eastl::string MakeArtifactPath(const AssetMountPoint& mount, uint64_t main_guid, const ProducedArtifact& artifact) {
    return tryengine::fmt::format("{}/{}", mount.artifacts_dir.c_str(),
                                  MakeArtifactRelPath(main_guid, artifact).c_str());
}

uint64_t HashBytes(eastl::span<const uint8_t> bytes) {
    return tryengine::core::Hash64(eastl::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

eastl::vector<uint64_t> CollectRuntimeSubGuids(const ImportResult& res) {
    eastl::vector<uint64_t> guids;
    guids.reserve(res.artifacts.size());
    for (const auto& artifact : res.artifacts)
        if (artifact.target == ArtifactTarget::Runtime)
            guids.push_back(artifact.sub_guid);
    return guids;
}

eastl::vector<eastl::string> CollectArtifactPaths(const ImportResult& res) {
    eastl::vector<eastl::string> paths;
    paths.reserve(res.artifacts.size());
    for (const auto& artifact : res.artifacts)
        paths.push_back(MakeArtifactRelPath(res.main_guid, artifact));
    return paths;
}

// Stat внешних зависимостей импорта (модулей Slang, include-файлов). Ошибка stat не
// фатальна: зависимость останется в штампе с нулевым размером, и следующий запуск
// переимпортирует ассет.
Task<eastl::vector<ImportDependency>> CollectDependencyStats(const eastl::vector<ImportDependencyRef>& refs,
                                                             AsyncFileManager& file_manager) {
    eastl::vector<ImportDependency> dependencies;
    dependencies.reserve(refs.size());
    if (refs.empty())
        co_return dependencies;

    eastl::vector<FileHandle> stat_handles;
    stat_handles.reserve(refs.size());
    for (const auto& ref : refs)
        stat_handles.push_back(file_manager.GetStatAsyncCopy(ref.path));

    co_await WaitAllFiles(stat_handles);

    for (size_t i = 0; i < refs.size(); ++i) {
        ImportDependency dependency{};
        dependency.name = refs[i].name;
        dependency.path = refs[i].path;

        if (stat_handles[i].IsFailed()) {
            LogWarn(LogCategory::Importer, "Не удалось получить stat зависимости {}", dependency.path.c_str());
        } else {
            dependency.size = stat_handles[i].GetFileSize();
            const FileTime mtime = stat_handles[i].GetMtime();
            dependency.mtime_sec = mtime.sec;
            dependency.mtime_nsec = mtime.nsec;
        }

        dependencies.push_back(eastl::move(dependency));
    }

    co_return dependencies;
}

// Проверяет, что внешние зависимости не изменились с момента импорта.
Task<bool> DependenciesMatch(const ImportStamp& stamp, AsyncFileManager& file_manager) {
    if (stamp.dependencies.empty())
        co_return true;

    eastl::vector<FileHandle> stat_handles;
    stat_handles.reserve(stamp.dependencies.size());
    for (const auto& dependency : stamp.dependencies)
        stat_handles.push_back(file_manager.GetStatAsyncCopy(dependency.path));

    co_await WaitAllFiles(stat_handles);

    for (size_t i = 0; i < stamp.dependencies.size(); ++i) {
        if (stat_handles[i].IsFailed())
            co_return false;

        const ImportDependency& dependency = stamp.dependencies[i];
        if (stat_handles[i].GetFileSize() != dependency.size)
            co_return false;

        const FileTime mtime = stat_handles[i].GetMtime();
        if (mtime.sec != dependency.mtime_sec || mtime.nsec != dependency.mtime_nsec)
            co_return false;
    }

    co_return true;
}

// Записывает все артефакты импорта и дожидается их завершения.
Task<void> WriteArtifacts(const AssetMountPoint& mount, const ImportResult& res, AsyncFileManager& file_manager) {
    eastl::vector<FileHandle> write_handles;
    write_handles.reserve(res.artifacts.size());

    for (const auto& artifact : res.artifacts) {
        auto path = MakeArtifactPath(mount, res.main_guid, artifact);
        LogInfo(LogCategory::Importer, "Write Path artifact: {}", path.c_str());
        write_handles.push_back(file_manager.WriteChunkAsyncCopy(path.c_str(), artifact.bytes));
    }

    co_await WaitAllFiles(write_handles);

    for (const auto& write_handle : write_handles)
        if (write_handle.IsFailed())
            co_return LogAndMakeError("Не удалось записать артефакт: {}", write_handle.GetPath().data());
    co_return {};
}

Task<bool> AllArtifactsExist(const AssetMountPoint& mount, const eastl::vector<eastl::string>& rel_paths,
                             AsyncFileManager& file_manager) {
    if (rel_paths.empty())
        co_return false;

    eastl::vector<FileHandle> stat_handles;
    stat_handles.reserve(rel_paths.size());
    for (const auto& rel_path : rel_paths) {
        auto path = tryengine::fmt::format("{}/{}", mount.artifacts_dir.c_str(), rel_path.c_str());
        stat_handles.push_back(file_manager.GetStatAsyncCopy(path));
    }

    co_await WaitAllFiles(stat_handles);

    for (const auto& stat_handle : stat_handles)
        if (stat_handle.IsFailed())
            co_return false;
    co_return true;
}

Task<eastl::optional<ImportStamp>> ReadStamp(const AssetMountPoint& mount, uint64_t guid,
                                             AsyncFileManager& file_manager) {
    auto stamp_path = MakeStampPath(mount, guid);

    FileHandle stat_handle = file_manager.GetStatAsyncCopy(stamp_path);
    co_await stat_handle;
    if (stat_handle.IsFailed())
        co_return eastl::nullopt;

    FileHandle read_handle = file_manager.ReadChunkAsyncCopy(stamp_path, 0, stat_handle.GetFileSize());
    co_await read_handle;
    if (read_handle.IsFailed())
        co_return eastl::nullopt;

    co_return Json::DeserializePartial<ImportStamp>(read_handle.GetData());
}

bool StampMatches(const ImportStamp& stamp, const IAssetImporter& importer, const FileHandle& asset_stat,
                  eastl::span<const uint8_t> meta_bytes) {
    if (stamp.format_version != kImportStampVersion)
        return false;
    if (tryengine::core::ToView(stamp.importer) != importer.GetName() ||
        stamp.importer_version != importer.GetVersion())
        return false;

    // Размер + mtime исходника. Это вход конвейера, его пишет пользователь, а не конвейер,
    // поэтому порядок асинхронных записей на эту проверку не влияет.
    if (stamp.source_size != asset_stat.GetFileSize())
        return false;
    const FileTime asset_mtime = asset_stat.GetMtime();
    if (stamp.source_mtime_sec != asset_mtime.sec || stamp.source_mtime_nsec != asset_mtime.nsec)
        return false;

    // Единственный content-hash: мета уже прочитана, а именно её правка означает
    // "пользователь поменял настройки импорта".
    return stamp.meta_hash == HashBytes(meta_bytes);
}

ImportStamp MakeStamp(const IAssetImporter& importer, uint64_t source_size, FileTime source_mtime, uint64_t meta_hash,
                      const ImportResult& res, eastl::vector<ImportDependency> dependencies) {
    ImportStamp stamp;
    stamp.format_version = kImportStampVersion;
    stamp.importer = eastl::string(importer.GetName().data(), importer.GetName().size());
    stamp.importer_version = importer.GetVersion();
    stamp.source_size = source_size;
    stamp.source_mtime_sec = source_mtime.sec;
    stamp.source_mtime_nsec = source_mtime.nsec;
    stamp.meta_hash = meta_hash;
    stamp.artifacts = CollectArtifactPaths(res);
    stamp.dependencies = eastl::move(dependencies);
    return stamp;
}

Task<void> WriteStamp(const AssetMountPoint& mount, uint64_t guid, const ImportStamp& stamp,
                      AsyncFileManager& file_manager) {
    auto bytes = Json::Serialize(stamp);
    if (!bytes.has_value())
        co_return LogAndMakeError("Не удалось сериализовать штамп импорта");

    auto stamp_path = MakeStampPath(mount, guid);
    FileHandle write_handle = file_manager.WriteChunkAsyncCopy(stamp_path.c_str(), bytes.value());
    co_await write_handle;
    if (write_handle.IsFailed())
        co_return LogAndMakeError("Не удалось записать штамп импорта: {}", stamp_path.c_str());
    co_return {};
}

}  // namespace

Task<void> AssetPipeline::ImportOrphanAsset(eastl::string asset_path, AssetMountPoint mount,
                                            AsyncFileManager& file_manager) {
    // 1. Получение размера файла
    FileHandle stat_handle = file_manager.GetStatAsyncCopy(asset_path);
    co_await stat_handle;

    if (stat_handle.IsFailed())
        co_return LogAndMakeError("Не удалось получить stat для ассета-сироты: {}", asset_path.c_str());

    // 2. Чтение ассета
    FileHandle read_handle = file_manager.ReadChunkAsyncCopy(asset_path, 0, stat_handle.GetFileSize());
    co_await read_handle;

    if (read_handle.IsFailed())
        co_return LogAndMakeError("Не удалось прочитать ассет-сироту: {}", asset_path.c_str());

    // 3. Поиск импортера
    const size_t dot_pos = asset_path.rfind('.');
    if (dot_pos == eastl::string::npos)
        co_return LogAndMakeError("Файл не имеет расширения: {}", asset_path.c_str());

    const auto ext = asset_path.substr(dot_pos);
    IAssetImporter* importer = import_system_.GetImporterByExtension(ext.c_str());
    if (!importer)
        co_return LogAndMakeError("Не найден импортер для расширения: {}", asset_path.c_str());

    // 4. Импорт
    ImportContext ctx(asset_path, read_handle.GetData());
    auto import_res = co_await importer->Import(ctx);
    if (!import_res.has_value()) {
        LogError(LogCategory::Importer, "Ошибка импорта ассета-сироты {}: {}", asset_path.c_str(),
                 import_res.error().Message());
        co_return eastl::move(import_res.error());
    }

    const auto& res = *import_res;

    // 5. Запись артефактов и мета-файла
    auto artifacts_result = co_await WriteArtifacts(mount, res, file_manager);
    if (!artifacts_result.has_value())
        co_return eastl::move(artifacts_result.error());

    bool meta_written = false;
    uint64_t meta_hash = 0;
    if (!res.meta_bytes.empty()) {
        auto meta_path = tryengine::fmt::format("{}{}", asset_path.c_str(), meta_ext);
        FileHandle meta_write = file_manager.WriteChunkAsyncCopy(meta_path.c_str(), res.meta_bytes);
        co_await meta_write;

        if (meta_write.IsFailed())
            co_return LogAndMakeError("Не удалось записать мета-файл: {}", meta_path.c_str());

        meta_written = true;
        meta_hash = HashBytes(res.meta_bytes);
    }

    // 6. Штамп импорта — запись о том, что именно лежит на диске. Порядок записей не важен:
    //    в следующий раз штамп сравнивается по содержимому, а не по mtime.
    if (meta_written) {
        auto dependencies_result = co_await CollectDependencyStats(res.dependencies, file_manager);
        if (!dependencies_result.has_value())
            co_return eastl::move(dependencies_result.error());

        const ImportStamp stamp = MakeStamp(*importer, stat_handle.GetFileSize(), stat_handle.GetMtime(), meta_hash,
                                            res, eastl::move(*dependencies_result));
        auto stamp_result = co_await WriteStamp(mount, res.main_guid, stamp, file_manager);
        if (!stamp_result.has_value())
            co_return eastl::move(stamp_result.error());
    } else {
        // Без меты guid не сохранился: при следующем сканировании ассет снова будет сиротой.
        LogError(LogCategory::Importer, "Импортер не выдал мету для {}: штамп не записан", asset_path.c_str());
    }

    // 7. Регистрация артефактов и обновление карт под мьютексом
    RegisterArtifactPaths(mount, res.main_guid, CollectRuntimeSubGuids(res));

    {
        std::lock_guard lock(db_mutex_);
        path_to_guid_[PathHash(ToView(asset_path))] = res.main_guid;
    }

    LogInfo(LogCategory::Importer, "Ассет-сирота успешно импортирован: {}", asset_path.c_str());
    co_return {};
}

Task<void> AssetPipeline::ProcessExistingAsset(DiscoveredAssetPair asset_pair, AssetMountPoint mount,
                                               AsyncFileManager& file_manager) {
    const eastl::string& asset_path = asset_pair.asset_path;
    const eastl::string& meta_path = asset_pair.meta_path;

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

    auto header_res = tryengine::resources::Json::DeserializePartial<HeaderOnly>(meta_read.GetData());
    if (!header_res.has_value())
        co_return LogAndMakeError("Ошибка парсинга заголовка мета-файла: {}", meta_path.c_str());

    const auto& header = header_res->header;

    {
        std::lock_guard lock(db_mutex_);
        path_to_guid_[PathHash(ToView(asset_path))] = header.guid;
    }

    RegisterArtifactPaths(mount, header.guid, header.sub_assets);

    IAssetImporter* importer = import_system_.GetImporterByName(header.importer_type);
    if (!importer)
        co_return LogAndMakeError("Не найден импортер для типа: {}, путь {}", header.importer_type.c_str(), asset_path);

    // 3. Проверка актуальности. Сравниваем входы с записанными в штампе импорта; mtime
    //    меты и артефактов не участвует — их пишем мы сами, асинхронно и в любом порядке.
    bool need_reimport = false;

    auto stamp_result = co_await ReadStamp(mount, header.guid, file_manager);
    if (!stamp_result.has_value())
        co_return eastl::move(stamp_result.error());

    const eastl::optional<ImportStamp>& stamp = *stamp_result;
    if (!stamp.has_value()) {
        need_reimport = true;
        LogInfo(LogCategory::Importer, "Reimport {}: нет штампа импорта", asset_path.c_str());
    } else if (!StampMatches(*stamp, *importer, asset_stat, meta_read.GetData())) {
        need_reimport = true;
        LogInfo(LogCategory::Importer, "Reimport {}: изменился исходник, мета или версия импортера",
                asset_path.c_str());
    } else {
        auto artifacts_exist_result = co_await AllArtifactsExist(mount, stamp->artifacts, file_manager);
        if (!artifacts_exist_result.has_value())
            co_return eastl::move(artifacts_exist_result.error());

        if (!*artifacts_exist_result) {
            need_reimport = true;
            LogInfo(LogCategory::Importer, "Reimport {}: не хватает артефакта", asset_path.c_str());
        } else {
            auto dependencies_match = co_await DependenciesMatch(*stamp, file_manager);
            if (!dependencies_match.has_value())
                co_return eastl::move(dependencies_match.error());

            if (!*dependencies_match) {
                need_reimport = true;
                LogInfo(LogCategory::Importer, "Reimport {}: изменилась внешняя зависимость", asset_path.c_str());
            }
        }
    }

    if (!need_reimport)
        co_return {};

    // 4. Повторный импорт устаревшего ассета
    FileHandle asset_read = file_manager.ReadChunkAsyncCopy(asset_path, 0, asset_stat.GetFileSize());
    co_await asset_read;

    if (asset_read.IsFailed())
        co_return LogAndMakeError("Не удалось прочитать файл ассета для реимпорта: {}", asset_path.c_str());

    ImportContext ctx(asset_path, asset_read.GetData(), meta_read.GetData());
    auto import_res = co_await importer->Import(ctx);

    if (!import_res.has_value()) {
        LogError(LogCategory::Importer, "Ошибка реимпорта для {}: {}", asset_path.c_str(),
                 import_res.error().Message());
        co_return eastl::move(import_res.error());
    }

    const auto& res = *import_res;

    auto artifacts_result = co_await WriteArtifacts(mount, res, file_manager);
    if (!artifacts_result.has_value())
        co_return eastl::move(artifacts_result.error());

    // 5. Мета — тоже выход импорта (в ней, например, обновляется список sub_assets). Пишем
    //    только когда содержимое реально изменилось, и только если импортёр сохранил guid:
    //    иначе перезапись потеряла бы пользовательские настройки и адресацию ассета.
    uint64_t meta_hash = HashBytes(meta_read.GetData());
    if (!res.meta_bytes.empty() && HashBytes(res.meta_bytes) != meta_hash) {
        if (res.main_guid != header.guid) {
            LogError(LogCategory::Importer, "Импортер сменил guid при реимпорте {} ({} -> {}): мета не перезаписана",
                     asset_path.c_str(), header.guid, res.main_guid);
        } else {
            FileHandle meta_write = file_manager.WriteChunkAsyncCopy(meta_path, res.meta_bytes);
            co_await meta_write;
            if (meta_write.IsFailed())
                co_return LogAndMakeError("Не удалось записать мета-файл: {}", meta_path.c_str());

            meta_hash = HashBytes(res.meta_bytes);
        }
    }

    // 6. Штамп. Хеш меты — тот, что теперь лежит на диске. Штамп сравнивается по
    //    содержимому, поэтому порядок его записи относительно артефактов и меты не важен.
    auto dependencies_result = co_await CollectDependencyStats(res.dependencies, file_manager);
    if (!dependencies_result.has_value())
        co_return eastl::move(dependencies_result.error());

    const ImportStamp new_stamp = MakeStamp(*importer, asset_stat.GetFileSize(), asset_stat.GetMtime(), meta_hash, res,
                                            eastl::move(*dependencies_result));
    auto stamp_write_result = co_await WriteStamp(mount, res.main_guid, new_stamp, file_manager);
    if (!stamp_write_result.has_value())
        co_return eastl::move(stamp_write_result.error());

    {
        std::lock_guard lock(db_mutex_);
        path_to_guid_[PathHash(ToView(asset_path))] = res.main_guid;
    }

    RegisterArtifactPaths(mount, res.main_guid, CollectRuntimeSubGuids(res));
    co_return {};
}

// ============================================================================
// Мутация дерева от FileWatcher: потоки-источники только кладут команды в очередь,
// а применяет их main thread в ApplyPendingTreeChanges() (начало кадра).
// ============================================================================

namespace {

struct PathParts {
    eastl::string_view parent;
    eastl::string_view name;
};

PathParts SplitPath(eastl::string_view full) {
    while (full.size() > 1 && (full.back() == '/' || full.back() == '\\'))
        full.remove_suffix(1);
    const size_t slash = full.find_last_of("\\/");
    if (slash == eastl::string_view::npos)
        return {eastl::string_view(), full};
    return {full.substr(0, slash), full.substr(slash + 1)};
}

}  // namespace

void AssetPipeline::Post(TreeCommand::Kind kind, eastl::string_view a, eastl::string_view b, uint64_t guid) {
    TreeCommand cmd{kind, strings_.Intern(a), b.empty() ? nullptr : strings_.Intern(b), guid};
    std::lock_guard lock(pending_mutex_);
    pending_.push_back(cmd);
}

void AssetPipeline::OnFileCreatedOrModified(const char* path) {
    // TODO: новый/изменённый файл => импорт (создание .meta, guid) => PostAssetReady(path, guid).
    // Дерево нельзя пополнять до того, как известен guid.
    (void) path;
}

void AssetPipeline::OnFileDeleted(const char* path) {
    Post(TreeCommand::Kind::Deleted, eastl::string_view(path), {}, 0);
}

void AssetPipeline::OnFileRenamed(const char* old_path, const char* new_path) {
    Post(TreeCommand::Kind::Renamed, eastl::string_view(old_path), eastl::string_view(new_path), 0);
}

void AssetPipeline::PostAssetReady(const char* path, uint64_t guid) {
    Post(TreeCommand::Kind::AssetReady, eastl::string_view(path), {}, guid);
}

void AssetPipeline::ApplyPendingTreeChanges() {
    eastl::vector<TreeCommand> batch;
    {
        std::lock_guard lock(pending_mutex_);
        batch.swap(pending_);
    }
    if (batch.empty())
        return;

    UiTree::Changes changes;
    for (const TreeCommand& cmd : batch) {
        switch (cmd.kind) {
            case TreeCommand::Kind::Deleted:
                ApplyDeleted(StringTable::View(cmd.a), changes);
                break;
            case TreeCommand::Kind::Renamed:
                ApplyRenamed(StringTable::View(cmd.a), StringTable::View(cmd.b), changes);
                break;
            case TreeCommand::Kind::AssetReady:
                ApplyAssetReady(StringTable::View(cmd.a), cmd.guid, changes);
                break;
        }
        ApplyPathChanges(changes);  // после КАЖДОЙ команды: следующая может зависеть от предыдущей
        strings_.Release(cmd.a);
        strings_.Release(cmd.b);
    }
}

bool AssetPipeline::LocateFolder(eastl::string_view path, UiTree*& tree, uint32_t& idx) {
    for (UiTree* t : {&game_tree_, &engine_tree_}) {
        const uint32_t i = t->FindFolder(path);
        if (i != kNoFolder) {
            tree = t;
            idx = i;
            return true;
        }
    }
    return false;
}

void AssetPipeline::ApplyDeleted(eastl::string_view path, UiTree::Changes& changes) {
    UiTree* tree = nullptr;
    uint32_t idx = kNoFolder;

    if (LocateFolder(path, tree, idx)) {  // удалена папка
        tree->RemoveFolder(idx, &changes);
        return;
    }
    const PathParts p = SplitPath(path);  // удалён файл
    if (LocateFolder(p.parent, tree, idx))
        tree->RemoveFile(idx, p.name, &changes);
}

void AssetPipeline::ApplyRenamed(eastl::string_view old_path, eastl::string_view new_path, UiTree::Changes& changes) {
    const PathParts from = SplitPath(old_path);
    const PathParts to = SplitPath(new_path);
    const bool same_parent = PathHash(from.parent) == PathHash(to.parent);

    UiTree* tree = nullptr;
    uint32_t idx = kNoFolder;

    if (LocateFolder(old_path, tree, idx)) {  // папка
        if (!same_parent) {
            const uint32_t new_parent = tree->EnsureFolder(to.parent);
            if (new_parent == kNoFolder || !tree->MoveFolder(idx, new_parent, &changes))
                return;
        }
        tree->RenameFolder(idx, to.name, &changes);
        return;
    }

    if (!LocateFolder(from.parent, tree, idx))  // файл
        return;
    const uint32_t src_folder = idx;
    if (!same_parent) {
        const uint32_t dst_folder = tree->EnsureFolder(to.parent);
        if (dst_folder == kNoFolder || !tree->MoveFile(src_folder, from.name, dst_folder, &changes))
            return;
        idx = dst_folder;
    }
    tree->RenameFile(idx, from.name, to.name, &changes);
}

void AssetPipeline::ApplyAssetReady(eastl::string_view path, uint64_t guid, UiTree::Changes&) {
    const PathParts p = SplitPath(path);
    for (UiTree* t : {&game_tree_, &engine_tree_}) {
        const uint32_t folder = t->EnsureFolder(p.parent);  // kNoFolder, если путь не из этого дерева
        if (folder == kNoFolder)
            continue;
        t->AddFile(folder, p.name, guid);
        std::lock_guard lock(db_mutex_);
        path_to_guid_[PathHash(path)] = guid;
        return;
    }
}

// Синхронизирует path_to_guid_ с тем, что дерево переименовало/удалило.
void AssetPipeline::ApplyPathChanges(UiTree::Changes& changes) {
    if (changes.empty())
        return;
    {
        std::lock_guard lock(db_mutex_);
        for (const UiTree::PathChange& c : changes) {
            auto it = path_to_guid_.find(c.old_hash);
            if (it == path_to_guid_.end())
                continue;
            const uint64_t guid = it->second;
            path_to_guid_.erase(it);
            if (c.new_hash != 0)
                path_to_guid_[c.new_hash] = guid;
        }
    }
    changes.clear();
}

}  // namespace tryeditor