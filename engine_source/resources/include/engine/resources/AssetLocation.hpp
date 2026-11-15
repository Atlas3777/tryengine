#pragma once

#include <EASTL/string.h>
#include <EASTL/variant.h>
#include <cstdint>

namespace tryengine::resources {

using PakHandle = uint32_t;
constexpr PakHandle INVALID_PAK_HANDLE = UINT32_MAX;

// Откуда пришёл loose-ресурс. У авторитетного файла id выведен из пути, у импортируемого —
// это guid из .meta; редактору это нужно, чтобы решить, можно ли править материал.
enum class AssetOrigin : uint8_t { Authoritative = 0, Imported = 1 };

struct LooseFileLocation {
    eastl::string path;
    AssetOrigin origin{AssetOrigin::Imported};
};

struct PakChunkLocation {
    PakHandle pak_handle{INVALID_PAK_HANDLE};
    uint32_t fd_slot{UINT32_MAX};
    uint64_t offset{0};
    uint64_t size{0};
};

using AssetLocation = eastl::variant<LooseFileLocation, PakChunkLocation>;

}  // namespace tryengine::resources