#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <mutex>

#include "editor/FileWatcher.hpp"
#include "editor/gui/UiFile.hpp"
#include "engine/async/Task.hpp"
#include "import/ImportSystem.hpp"

namespace tryengine::resources {
class AsyncFileManager;
class AssetRegistry;
}  // namespace tryengine::resources

namespace tryeditor {

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

class AssetPipeline {
public:
    enum class StorageDomain : uint8_t { Engine = 0, Game = 1 };

    AssetPipeline(tryengine::resources::AssetRegistry& registry);
    ~AssetPipeline() = default;

    eastl::vector<UiFolder>& GetFoldersMutable(StorageDomain domain);

    tryengine::async::Task<void> InitEngineContentSync(tryengine::resources::AsyncFileManager& file_manager);
    tryengine::async::Task<void> AsyncLoadGameContent(tryengine::resources::AsyncFileManager& file_manager);

    void OnFileCreatedOrModified(const char* path);
    void OnFileDeleted(const char* path);

    eastl::vector<UiFolder> engine_folders_;
    eastl::vector<UiFolder> game_folders_;

    ImportSystem& GetImportSystem(){return import_system_;}

private:
    tryengine::async::Task<void> ProcessDomain(DomainScanResult& scan_data,
                                               tryengine::resources::AsyncFileManager& file_manager,
                                               StorageDomain domain);

    tryengine::async::Task<void> ImportOrphanAsset(eastl::string asset_path, AssetMountPoint mount,
                                                   tryengine::resources::AsyncFileManager& file_manager);

    tryengine::async::Task<void> ProcessExistingAsset(eastl::string asset_path, eastl::string meta_path,
                                                      AssetMountPoint mount,
                                                      tryengine::resources::AsyncFileManager& file_manager);

    void RegisterArtifactPaths(const AssetMountPoint& mount, uint64_t main_guid,
                               const eastl::vector<uint64_t>& sub_assets) const;

    mutable std::mutex db_mutex_;
    eastl::hash_map<uint64_t, eastl::string> guid_to_path_;
    eastl::hash_map<eastl::string, uint64_t> path_to_guid_;

    FileWatcher file_watcher_;
    ImportSystem import_system_;
    tryengine::resources::AssetRegistry& registry_;
};

}  // namespace tryeditor