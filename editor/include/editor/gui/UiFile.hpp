#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

namespace tryeditor {
struct UiFile {
    eastl::string name;  // Имя для отображения в UI (например, "player.png")
    uint64_t guid;       // Guid пока пустой
    uint32_t asset_idx;  // Индекс в плоском массиве ресурсов движка

    const char* GetName() const { return name.c_str(); };
};

struct UiFolder {
    eastl::string name;  // Имя папки
    eastl::vector<uint32_t> subfolders;  // Индексы дочерних папок в глобальном векторе
    eastl::vector<UiFile> files;         // Только файлы внутри этой папки

    const char* GetName() const { return name.c_str(); };
};
}  // namespace tryeditor