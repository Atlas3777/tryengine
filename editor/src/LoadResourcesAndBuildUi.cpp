#include <EASTL/functional.h>
#include <EASTL/hash_map.h>

#include "editor/gui/LoadResourcesAndBuildUi.h"
#include "engine/resources/FileIterator.h"

namespace tryeditor {

struct WaitingAsset {
    eastl::string full_path;
    eastl::string name;
    uint32_t parent_ui_idx;
    bool is_meta;
};

void LoadResourcesAndBuildUi(const eastl::string_view root_dir, eastl::vector<eastl::string>& out_assets,
                             eastl::vector<eastl::string>& out_metas, eastl::vector<eastl::string>& orphan_assets,
                             eastl::vector<eastl::string>& orphan_metas, eastl::vector<UiFolder>& out_folders) {
    // Хэш-карта ожидания. Ключ — хэш пути к ассету без расширения .meta
    eastl::hash_map<uint64_t, WaitingAsset> waiting_room;

    // Выделяем имя корневой директории
    size_t root_slash = eastl::string_view(root_dir).find_last_of("\\/");
    eastl::string_view root_name =
        (root_slash == eastl::string_view::npos) ? root_dir : root_dir.substr(root_slash + 1);

    // Инициализируем корень дерева папок (индекс 0)
    out_folders.push_back({eastl::string(root_name.data(), root_name.size()), {}, {}});
    uint32_t root_folder_idx = 0;

    tryengine::filesystem::Iterate(
        root_dir, root_folder_idx,
        [&](const eastl::string& path_str, const char* name, bool is_dir, uint32_t parent_idx) -> uint32_t {
            if (is_dir) {
                uint32_t new_folder_idx = static_cast<uint32_t>(out_folders.size());
                out_folders.push_back({eastl::string(name), {}, {}});

                // Связываем текущую папку с её родителем
                out_folders[parent_idx].subfolders.push_back(new_folder_idx);
                return new_folder_idx;
            }

            const size_t path_size = path_str.size();
            const size_t name_size = strlen(name);
            const bool is_meta = (name_size >= 5 && strcmp(name + name_size - 5, ".meta") == 0);

            // Получаем string_view пути к ассету (без .meta, если это мета-файл)
            eastl::string_view asset_path_view = is_meta ? eastl::string_view(path_str.data(), path_size - 5)
                                                         : eastl::string_view(path_str.data(), path_size);

            // Считаем хэш от view без единой аллокации
            uint64_t asset_path_hash = eastl::hash<eastl::string_view>{}(asset_path_view);

            auto it = waiting_room.find(asset_path_hash);

            if (it != waiting_room.end()) {
                uint32_t asset_idx = 0;
                eastl::string file_name;

                if (is_meta) {
                    // Пришла мета, а в карте уже лежал сам ассет
                    out_assets.push_back(eastl::move(it->second.full_path));
                    out_metas.push_back(path_str);
                    file_name = eastl::move(it->second.name);
                } else {
                    // Пришел ассет, а в карте уже лежала его мета
                    out_assets.push_back(path_str);
                    out_metas.push_back(eastl::move(it->second.full_path));
                    file_name = eastl::string(name);
                }
                asset_idx = static_cast<uint32_t>(out_assets.size() - 1);

                // Добавляем файл в список файлов конкретно этой папки
                out_folders[parent_idx].files.push_back({eastl::move(file_name),
                                                         0,  // guid
                                                         asset_idx});

                waiting_room.erase(it);
            } else {
                // Первый из пары (или сирота) — сохраняем данные
                waiting_room[asset_path_hash] = {path_str, eastl::string(name), parent_idx, is_meta};
            }

            return parent_idx;
        });

    for (auto& pair : waiting_room) {
        WaitingAsset& orphan = pair.second;

        if (!orphan.is_meta) {
            orphan_assets.push_back(orphan.full_path);
        } else {
            orphan_metas.push_back(orphan.full_path);
        }
    }
}
}  // namespace tryeditor