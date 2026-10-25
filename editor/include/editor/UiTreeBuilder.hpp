#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include "editor/AssetScanner.hpp"
#include "engine/core/Inline.hpp"

namespace tryeditor {

struct UiFile {
    eastl::string name;  // Имя для отображения: "player.scn"
    eastl::string path;  // Честный путь к файлу: "game/assets/scenes/player.scn"
    uint64_t guid = 0;   // Валидный GUID из .meta (0, если ещё не импортирован)

    __forceinline const char* GetName() const { return name.c_str(); }
    __forceinline const char* GetPath() const { return path.c_str(); }
};

struct UiFileTest {
    uint64_t guid;
};

struct UiFolder {
    eastl::string name;                  // Имя папки: "scenes"
    eastl::string path;                  // Полный путь: "game/assets/scenes"
    eastl::vector<uint32_t> subfolders;  // Индексы дочерних папок в глобальном векторе UiFolder
    eastl::vector<UiFile> files;         // Честные, самодостаточные файлы внутри этой папки

    __forceinline const char* GetName() const { return name.c_str(); }
    __forceinline const char* GetPath() const { return path.c_str(); }
};


class UiTreeBuilder {
public:
    static eastl::vector<UiFolder> BuildUiTree(
        const eastl::vector<RawFolder>& raw_folders,
        const eastl::hash_map<eastl::string, uint64_t>& path_to_guid);
};

}  // namespace tryeditor