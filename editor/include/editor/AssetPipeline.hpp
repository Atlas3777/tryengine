#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <mutex>

#include "UiTree.hpp"
#include "editor/AssetScanner.hpp"
#include "editor/FileWatcher.hpp"
#include "engine/async/Task.hpp"
#include "engine/core/strings/StringTable.hpp"
#include "import/ImportSystem.hpp"

namespace tryengine::resources {
class AsyncFileManager;
class AssetRegistry;
}  // namespace tryengine::resources

namespace tryeditor {

// Корни, которые редактор показывает деревьями. Домен — это корень, а не тип файла.
enum class StorageDomain : uint8_t { Engine = 0, Game = 1, Content = 2, Data = 3 };

// Ассеты (engine, game): у файла есть .meta с guid и артефакт импорта, на диск он попадает
// через конвейер. Авторитетные файлы (content, data) — сами по себе: .meta, guid и
// артефактов у них нет, поэтому их деревья строятся одним сканированием.
inline constexpr bool IsAssetDomain(StorageDomain domain) {
    return domain == StorageDomain::Engine || domain == StorageDomain::Game;
}

// Где лежит корень домена. artifacts_dir == nullptr => артефактов нет.
struct StorageRoot {
    StorageDomain domain;
    const char* dir;
    const char* artifacts_dir;
};

inline constexpr StorageRoot kStorageRoots[] = {
    {StorageDomain::Engine, "engine_content/assets", "engine_content/artifacts"},
    {StorageDomain::Game, "game/assets", "game/artifacts"},
    {StorageDomain::Content, "game/content", nullptr},  // префабы и сцены: композиции
    {StorageDomain::Data, "game/data", nullptr},        // атомы: материалы и прочее
};

// Корень домена. Таблица выше — единственное место, где домен связан с каталогом.
const StorageRoot& RootOf(StorageDomain domain);

struct AssetMountPoint {
    eastl::string name;
    eastl::string assets_dir;
    eastl::string artifacts_dir;
};

class AssetPipeline {
public:
    AssetPipeline(tryengine::resources::AssetRegistry& registry);
    ~AssetPipeline() = default;

    const eastl::vector<UiFolder>& GetFolders(StorageDomain domain) const;
    const UiTree& GetTree(StorageDomain domain) const;

    // Ревизия дерева домена: поднимается на любом его изменении. daslang по ней решает,
    // повторять ли обход (кеш запросов "все материалы в data").
    uint64_t GetTreeRevision(StorageDomain domain) const;

    // Деревья авторитетных доменов (content, data). Ни мет, ни guid, ни артефактов нет,
    // поэтому это одно сканирование диска без конвейера импорта. main thread.
    void BuildAuthoritativeTrees();

    // Полная пересборка всех деревьев из диска — пока файлового вотчера нет, это
    // единственный способ увидеть новые файлы. main thread, до отрисовки дерева.
    void RebuildTrees();

    tryengine::async::Task<void> ImportEngineContent(tryengine::resources::AsyncFileManager& file_manager);
    tryengine::async::Task<void> ImportGameContent(tryengine::resources::AsyncFileManager& file_manager);

    // --- Вызываются из ЛЮБОГО потока (FileWatcher). Только кладут команду в очередь. ---
    void OnFileCreatedOrModified(const char* path);
    void OnFileDeleted(const char* path);
    void OnFileRenamed(const char* old_path, const char* new_path);  // rename И move (файл или папка)
    void PostAssetReady(const char* path, uint64_t guid);            // ассет импортирован => в дерево

    // --- ТОЛЬКО main thread, в начале кадра, ДО отрисовки das-дерева. Единственное место мутаций. ---
    void ApplyPendingTreeChanges();

    ImportSystem& GetImportSystem() {
        return import_system_;
    }

private:
    struct TreeCommand {
        enum class Kind : uint8_t { AssetReady, Deleted, Renamed };
        Kind kind;
        const char* a;  // интернированные строки, освобождаются после применения
        const char* b;
        uint64_t guid;
    };

    void Post(TreeCommand::Kind kind, eastl::string_view a, eastl::string_view b, uint64_t guid);
    bool LocateFolder(eastl::string_view path, UiTree*& tree, uint32_t& idx);
    void ApplyDeleted(eastl::string_view path, UiTree::Changes& changes);
    void ApplyRenamed(eastl::string_view old_path, eastl::string_view new_path, UiTree::Changes& changes);
    void ApplyAssetReady(eastl::string_view path, uint64_t guid, UiTree::Changes& changes);
    void ApplyPathChanges(UiTree::Changes& changes);

    UiTree& Tree(StorageDomain domain);
    void BuildTreeFromDisk(StorageDomain domain);

    tryengine::async::Task<void> ProcessDomain(const AssetMountPoint& mount, ScanResult scan_data,
                                               tryengine::resources::AsyncFileManager& file_manager,
                                               StorageDomain domain);

    tryengine::async::Task<void> ImportOrphanAsset(eastl::string asset_path, AssetMountPoint mount,
                                                   tryengine::resources::AsyncFileManager& file_manager);

    tryengine::async::Task<void> ProcessExistingAsset(DiscoveredAssetPair asset_pair, AssetMountPoint mount,
                                                      tryengine::resources::AsyncFileManager& file_manager);

    void RegisterArtifactPaths(const AssetMountPoint& mount, uint64_t main_guid,
                               const eastl::vector<uint64_t>& sub_assets) const;

    mutable std::mutex db_mutex_;
    eastl::hash_map<uint64_t, uint64_t> path_to_guid_;  // ключ: PathId(путь)

    // ВАЖНО: порядок объявления. strings_ должна быть раньше деревьев (умирает позже них).
    tryengine::core::StringTable strings_;
    UiTree engine_tree_{strings_};
    UiTree game_tree_{strings_};
    UiTree content_tree_{strings_};
    UiTree data_tree_{strings_};

    std::mutex pending_mutex_;
    eastl::vector<TreeCommand> pending_;

    FileWatcher file_watcher_;
    ImportSystem import_system_;
    tryengine::resources::AssetRegistry& registry_;
};

}  // namespace tryeditor