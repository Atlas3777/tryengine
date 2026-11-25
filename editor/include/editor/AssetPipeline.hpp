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
// Новые значения добавляются в конец: int(domain) используется как индекс в скриптовых
// таблицах, а значения старых доменов не должны меняться.
enum class StorageDomain : uint8_t {
    Engine = 0,
    Game = 1,
    Content = 2,
    Data = 3,
    EngineShaders = 4,
    GameShaders = 5,
};

// Ассеты (engine/game assets и shaders): у файла есть .meta с guid и артефакт импорта, на
// диск он попадает через конвейер. Авторитетные файлы (content, data) — сами по себе:
// .meta, guid и артефактов у них нет, поэтому их деревья строятся одним сканированием.
inline constexpr bool IsAssetDomain(StorageDomain domain) {
    return domain == StorageDomain::Engine || domain == StorageDomain::Game || domain == StorageDomain::EngineShaders ||
           domain == StorageDomain::GameShaders;
}

// Где лежит корень домена. artifacts_dir == nullptr => артефактов нет.
struct StorageRoot {
    StorageDomain domain;
    const char* dir;
    const char* artifacts_dir;
};

inline constexpr StorageRoot kStorageRoots[] = {
    {StorageDomain::Engine, "engine_content/assets", "engine_content/artifacts"},
    {StorageDomain::EngineShaders, "engine_content/shaders", "engine_content/artifacts"},
    {StorageDomain::Game, "game/assets", "game/artifacts"},
    {StorageDomain::GameShaders, "game/shaders", "game/artifacts"},
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

    //! Авторский путь импортируемого ассета по его id (обратная сторона path_to_guid_).
    //! Нужен панелям: guid в поле материала — не то, что можно прочитать глазами, а
    //! `GetAssetFilePath` для импортируемого ассета возвращает путь артефакта, не исходника.
    eastl::string SourcePathOf(uint64_t guid) const;

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
    // Возвращает guid'ы ассетов, которые только что переимпортировались: вызывающий
    // сбрасывает их ресурсы и зависимые материалы. Пустой список — изменений нет.
    eastl::vector<uint64_t> ApplyPendingTreeChanges();

    //! Ставит переимпорт одного ассета в очередь. Асинхронно: импорт пишет артефакты,
    //! мету и штамп, после чего ApplyPendingTreeChanges отдаст guid. false — путь не из
    //! asset-домена (у content/data нет ни .meta, ни импортёра).
    bool RequestReimport(eastl::string_view path, tryengine::resources::AsyncFileManager& file_manager);

    //! Растёт, когда ApplyPendingTreeChanges принял переимпорт. По ней панели понимают,
    //! что материал/меш на экране мог смениться, и перечитывают свои ресурсы.
    uint64_t ReimportGeneration() const {
        return reimport_generation_;
    }

    ImportSystem& GetImportSystem() {
        return import_system_;
    }

private:
    struct TreeCommand {
        enum class Kind : uint8_t { AssetReady, Deleted, Renamed, Reimported };
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

    // Импорт одного корня: скан, импорт сирот/устаревших ассетов, дерево. Общая часть
    // ImportEngineContent/ImportGameContent, чтобы assets и shaders шли одним конвейером.
    tryengine::async::Task<void> ImportDomain(StorageDomain domain,
                                              tryengine::resources::AsyncFileManager& file_manager);

    tryengine::async::Task<void> ImportOrphanAsset(eastl::string asset_path, AssetMountPoint mount,
                                                   tryengine::resources::AsyncFileManager& file_manager);

    tryengine::async::Task<void> ProcessExistingAsset(DiscoveredAssetPair asset_pair, AssetMountPoint mount,
                                                      tryengine::resources::AsyncFileManager& file_manager,
                                                      bool force = false);

    tryengine::async::Task<void> ReimportAsset(StorageDomain domain, eastl::string asset_path,
                                               tryengine::resources::AsyncFileManager& file_manager);

    void RegisterArtifactPaths(const AssetMountPoint& mount, uint64_t main_guid,
                               const eastl::vector<uint64_t>& sub_assets) const;

    // Editor-артефакты адресуются путём, а не guid: sub в их пути — вид артефакта
    // (10 — карта модели, 11 — префаб), одинаковый у всех ассетов. Регистрируем их
    // на старте, без реимпорта, иначе drag&drop модели не прочитает артефакт.
    void RegisterEditorArtifactPaths(const AssetMountPoint& mount, const eastl::vector<eastl::string>& rel_paths) const;

    mutable std::mutex db_mutex_;
    eastl::hash_map<uint64_t, uint64_t> path_to_guid_;  // ключ: PathId(путь)
    eastl::hash_map<uint64_t, eastl::string> guid_to_path_;

    // ВАЖНО: порядок объявления. strings_ должна быть раньше деревьев (умирает позже них).
    tryengine::core::StringTable strings_;
    UiTree engine_tree_{strings_};
    UiTree engine_shaders_tree_{strings_};
    UiTree game_tree_{strings_};
    UiTree game_shaders_tree_{strings_};
    UiTree content_tree_{strings_};
    UiTree data_tree_{strings_};

    std::mutex pending_mutex_;
    eastl::vector<TreeCommand> pending_;
    uint64_t reimport_generation_ = 0;

    FileWatcher file_watcher_;
    ImportSystem import_system_;
    tryengine::resources::AssetRegistry& registry_;
};

}  // namespace tryeditor