#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include "editor/FileWatcher.hpp"
#include "editor/gui/ResourceLoader.h"
#include "engine/async/Task.hpp"

namespace tryengine::resources {
class AsyncFileManager;
class AssetRegistry;
}

namespace tryeditor {
class ImportSystem;

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
        Engine = 0,
        Game = 1
    };

    AssetSourceDatabase(ImportSystem& import_system, tryengine::resources::AssetRegistry& registry);
    ~AssetSourceDatabase() = default;

    static eastl::vector<UiFolder>& GetFolders(StorageDomain domain);
    static eastl::vector<UiFolder>& GetFoldersMutable(StorageDomain domain);

    static eastl::vector<UiFolder>& GetEngineFolders() { return GetFolders(StorageDomain::Engine); }
    static eastl::vector<UiFolder>& GetGameFolders() { return GetFolders(StorageDomain::Game); }

    tryengine::async::Task<void> InitEngineContentSync(tryengine::resources::AsyncFileManager& file_manager);
    tryengine::async::Task<void> AsyncLoadGameContent(tryengine::resources::AsyncFileManager& file_manager);

    void OnFileCreatedOrModified(const char* path);
    void OnFileDeleted(const char* path);

private:
    tryengine::async::Task<void> ProcessDomain(DomainScanResult& scan_data, tryengine::resources::AsyncFileManager& file_manager, StorageDomain domain);

    // Вспомогательный метод для проставления GUID в UiFolder и заполнения хеш-карт
    tryengine::async::Task<void> ResolveExistingGuids(DomainScanResult& scan_data, tryengine::resources::AsyncFileManager& file_manager);

    tryengine::async::Task<void> ImportOrphans(DomainScanResult& scan_data, tryengine::resources::AsyncFileManager& file_manager) const;
    tryengine::async::Task<void> ReimportStaleAssets(const DomainScanResult& scan_data, tryengine::resources::AsyncFileManager& file_manager) const;

    void RegisterArtifactPaths(const AssetMountPoint& mount, uint64_t main_guid, const eastl::vector<uint64_t>& sub_assets) const;

    inline static eastl::vector<UiFolder> engine_folders_;
    inline static eastl::vector<UiFolder> game_folders_;

    eastl::hash_map<uint64_t, eastl::string> guid_to_path_;
    eastl::hash_map<eastl::string, uint64_t> path_to_guid_;

    FileWatcher file_watcher_;
    ImportSystem& import_system_;
    tryengine::resources::AssetRegistry& registry_;
};

}  // namespace tryeditor