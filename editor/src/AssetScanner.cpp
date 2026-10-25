#include "editor/AssetScanner.hpp"
#include "engine/resources/FileIterator.h"

#include <EASTL/functional.h>
#include <EASTL/hash_map.h>
#include <cstring>

namespace tryeditor {

struct WaitingAsset {
    eastl::string full_path;
    eastl::string name;
    uint32_t parent_folder_idx;
    bool is_meta;
};

ScanResult AssetScanner::ScanDirectory(eastl::string_view root_dir) {
    ScanResult result;
    eastl::hash_map<uint64_t, WaitingAsset> waiting_room;

    // Выделяем имя корневой директории
    size_t root_slash = root_dir.find_last_of("\\/");
    eastl::string_view root_name =
        (root_slash == eastl::string_view::npos) ? root_dir : root_dir.substr(root_slash + 1);

    // Корневая папка (индекс 0)
    result.raw_folders.push_back(RawFolder{
        .name = eastl::string(root_name.data(), root_name.size()),
        .path = eastl::string(root_dir.data(), root_dir.size()),
        .parent_idx = 0,
        .subfolders = {},
        .files = {}
    });
    uint32_t root_folder_idx = 0;

    tryengine::filesystem::Iterate(
        root_dir, root_folder_idx,
        [&](const eastl::string& path_str, const char* name, bool is_dir, uint32_t parent_idx) -> uint32_t {
            if (is_dir) {
                uint32_t new_folder_idx = static_cast<uint32_t>(result.raw_folders.size());
                result.raw_folders.push_back(RawFolder{
                    .name = eastl::string(name),
                    .path = path_str,
                    .parent_idx = parent_idx,
                    .subfolders = {},
                    .files = {}
                });

                result.raw_folders[parent_idx].subfolders.push_back(new_folder_idx);
                return new_folder_idx;
            }

            const size_t path_size = path_str.size();
            const size_t name_size = strlen(name);
            const bool is_meta = (name_size >= 5 && strcmp(name + name_size - 5, ".meta") == 0);

            // Если это файл ассета (не .meta), сохраняем в список файлов папки
            if (!is_meta) {
                result.raw_folders[parent_idx].files.push_back(RawFile{
                    .name = eastl::string(name),
                    .path = path_str
                });
            }

            // Получаем string_view пути к ассету (без .meta)
            eastl::string_view asset_path_view = is_meta ? eastl::string_view(path_str.data(), path_size - 5)
                                                         : eastl::string_view(path_str.data(), path_size);

            uint64_t asset_path_hash = eastl::hash<eastl::string_view>{}(asset_path_view);
            auto it = waiting_room.find(asset_path_hash);

            if (it != waiting_room.end()) {
                DiscoveredAssetPair pair;
                if (is_meta) {
                    pair.asset_path = eastl::move(it->second.full_path);
                    pair.meta_path = path_str;
                    pair.name = eastl::move(it->second.name);
                    pair.parent_folder_idx = it->second.parent_folder_idx;
                } else {
                    pair.asset_path = path_str;
                    pair.meta_path = eastl::move(it->second.full_path);
                    pair.name = eastl::string(name);
                    pair.parent_folder_idx = parent_idx;
                }

                result.matched_assets.push_back(eastl::move(pair));
                waiting_room.erase(it);
            } else {
                waiting_room[asset_path_hash] = WaitingAsset{path_str, eastl::string(name), parent_idx, is_meta};
            }

            return parent_idx;
        });

    // Оставшиеся файлы — сироты
    for (auto& entry : waiting_room) {
        WaitingAsset& orphan = entry.second;
        if (!orphan.is_meta) {
            result.orphan_assets.push_back(eastl::move(orphan.full_path));
        } else {
            result.orphan_metas.push_back(eastl::move(orphan.full_path));
        }
    }

    return result;
}

}  // namespace tryeditor