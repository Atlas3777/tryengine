#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>

namespace tryeditor {

struct RawFile {
    eastl::string name;  // Имя файла ("hero.png")
    eastl::string path;  // Путь к ассету ("game/assets/textures/hero.png")
};

struct RawFolder {
    eastl::string name;                  // Имя папки
    eastl::string path;                  //
    uint32_t parent_idx = 0;              // Индекс родительской папки
    eastl::vector<uint32_t> subfolders;  // Индексы дочерних папок
    eastl::vector<RawFile> files;        // Список ассет-файлов в этой папке
};

struct DiscoveredAssetPair {
    eastl::string asset_path;      // "game/assets/textures/hero.png"
    eastl::string meta_path;       // "game/assets/textures/hero.png.meta"
    eastl::string name;            // "hero.png"
    uint32_t parent_folder_idx =0;
};

struct ScanResult {
    eastl::vector<DiscoveredAssetPair> matched_assets; // Валидные пары (ассет + meta)
    eastl::vector<eastl::string> orphan_assets;        // Ассеты без meta
    eastl::vector<eastl::string> orphan_metas;         // Meta без ассета
    eastl::vector<RawFolder> raw_folders;              // Структура папок на диске
};

class AssetScanner {
public:
    static ScanResult ScanDirectory(eastl::string_view root_dir);
};

}  // namespace tryeditor