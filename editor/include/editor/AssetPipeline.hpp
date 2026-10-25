#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <mutex>

#include "UiTreeBuilder.hpp"
#include "editor/AssetScanner.hpp"
#include "editor/FileWatcher.hpp"
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

class AssetPipeline {
public:
    enum class StorageDomain : uint8_t { Engine = 0, Game = 1 };

    AssetPipeline(tryengine::resources::AssetRegistry& registry);
    ~AssetPipeline() = default;

    const eastl::vector<UiFolder>& GetFolders(StorageDomain domain) const;
    eastl::vector<UiFolder>& GetFoldersMutable(StorageDomain domain);

    tryengine::async::Task<void> ImportEngineContent(tryengine::resources::AsyncFileManager& file_manager);
    tryengine::async::Task<void> ImportGameContent(tryengine::resources::AsyncFileManager& file_manager);

    void OnFileCreatedOrModified(const char* path);
    void OnFileDeleted(const char* path);

    ImportSystem& GetImportSystem() { return import_system_; }

private:
    tryengine::async::Task<void> ProcessDomain(const AssetMountPoint& mount,
                                               ScanResult scan_data,
                                               tryengine::resources::AsyncFileManager& file_manager,
                                               StorageDomain domain);

    tryengine::async::Task<void> ImportOrphanAsset(eastl::string asset_path, AssetMountPoint mount,
                                                   tryengine::resources::AsyncFileManager& file_manager);

    tryengine::async::Task<void> ProcessExistingAsset(DiscoveredAssetPair asset_pair,
                                                      AssetMountPoint mount,
                                                      tryengine::resources::AsyncFileManager& file_manager);

    void RegisterArtifactPaths(const AssetMountPoint& mount, uint64_t main_guid,
                               const eastl::vector<uint64_t>& sub_assets) const;

    mutable std::mutex db_mutex_;
    eastl::hash_map<uint64_t, eastl::string> guid_to_path_;
    eastl::hash_map<eastl::string, uint64_t> path_to_guid_;

    eastl::vector<UiFolder> engine_folders_;
    eastl::vector<UiFolder> game_folders_;

    FileWatcher file_watcher_;
    ImportSystem import_system_;
    tryengine::resources::AssetRegistry& registry_;
};

}  // namespace tryeditor