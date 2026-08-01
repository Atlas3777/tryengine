#pragma once

#include "editor/FileWatcher.hpp"
#include "editor/gui/ResourceLoader.h"
#include "engine/async/Task.hpp"
#include "engine/graphics/ResourceHandle.hpp"

namespace tryengine::resources {
class AsyncFileManager;
}

namespace tryeditor {
class ImportSystem;

using tryengine::resources::ResourceHandle;

struct AssetMountPoint {
    eastl::string name;
    eastl::string assets_dir;
    eastl::string artifacts_dir;
};

struct DomainScanResult {
    AssetMountPoint mount;
    eastl::vector<eastl::string> assets;
    eastl::vector<eastl::string> metas;
    eastl::vector<eastl::string> orphan_assets;
    eastl::vector<eastl::string> orphan_metas;
    eastl::vector<UiFolder> folders;
};

class AssetSourceDatabase {
public:

    enum class StorageDomain : uint8_t {
        Engine,
        Game
    };

    AssetSourceDatabase(ImportSystem& import_system);
    ~AssetSourceDatabase() = default;

    static eastl::vector<UiFolder>& GetFolders(StorageDomain domain);
    static eastl::vector<UiFolder>& GetFoldersMutable(StorageDomain domain);

    // Дополнительные удобные алиасы
    static eastl::vector<UiFolder>& GetEngineFolders() { return GetFolders(StorageDomain::Engine); }
    static eastl::vector<UiFolder>& GetGameFolders() { return GetFolders(StorageDomain::Game); }

    // Синхронный старт контента движка (блокирует поток до полной готовности)
    tryengine::async::Task<void> InitEngineContentSync(tryengine::resources::AsyncFileManager& file_manager) const;

    // Асинхронный старт пользовательского контента
    tryengine::async::Task<void> AsyncLoadGameContent(tryengine::resources::AsyncFileManager& file_manager);

    template <typename T>
    ResourceHandle<T> Get(uint64_t id) {
        return ResourceHandle<T>{};
    }

    template <typename T>
    tryengine::async::Task<ResourceHandle<T>> GetAsync(uint64_t id) {
        co_return ResourceHandle<T>{};
    }

private:
    // Универсальные пайплайны, принимающие конкретный домен
    tryengine::async::Task<void> ProcessDomain(const DomainScanResult& scan_data, tryengine::resources::AsyncFileManager& file_manager) const;
    tryengine::async::Task<void> ImportOrphans(const DomainScanResult& scan_data, tryengine::resources::AsyncFileManager& file_manager) const;
    tryengine::async::Task<void> ReimportStaleAssets(const DomainScanResult& scan_data, tryengine::resources::AsyncFileManager& file_manager) const;

    inline static eastl::vector<UiFolder> engine_folders_;
    inline static eastl::vector<UiFolder> game_folders_;

    FileWatcher file_watcher_;
    ImportSystem& import_system_;
};

}  // namespace tryeditor