#pragma once

#include <cstdint>
#include <EASTL/string.h>
#include <EASTL/variant.h>

namespace tryengine::resources {

using PakHandle = uint32_t;
constexpr PakHandle INVALID_PAK_HANDLE = UINT32_MAX;

struct LooseFileLocation {
    eastl::string path;
};

struct PakChunkLocation {
    PakHandle pak_handle{INVALID_PAK_HANDLE};
    uint32_t fd_slot{UINT32_MAX};
    uint64_t offset{0};
    uint64_t size{0};
};

using AssetLocation = eastl::variant<LooseFileLocation, PakChunkLocation>;

} // namespace tryengine::resources