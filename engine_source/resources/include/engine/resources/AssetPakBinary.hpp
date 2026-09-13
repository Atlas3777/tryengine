#pragma once
#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>

#include "engine/core/Result.hpp"

namespace tryengine::resources {

constexpr uint32_t PAK_MAGIC = 0x4B415054; // 'TPAK'
constexpr uint32_t PAK_VERSION = 1;

struct ResourceLayout {
    uint64_t guid = 0;
    uint64_t offset = 0; // Смещение относительно data_offset
    uint64_t size = 0;
};

struct AssetPakHeader {
    uint32_t magic = PAK_MAGIC;
    uint32_t version = PAK_VERSION;
    uint64_t dependencies_offset = 0;
    uint64_t dependencies_size = 0;
    uint64_t resource_layout_offset = 0;
    uint64_t resource_layout_size = 0;
    uint64_t data_offset = 0;
    uint64_t data_size = 0;
    uint8_t _padding[8];
};

static_assert(sizeof(AssetPakHeader) == 64, "AssetPakHeader change size");

struct AssetPakMetadata {
    AssetPakHeader header;
    eastl::vector<eastl::string> dependencies;
    eastl::vector<ResourceLayout> layout;
};

class AssetPakBinary {
public:
    static Result<AssetPakHeader> ReadHeader(eastl::span<const uint8_t> buffer);
    static Result<AssetPakMetadata> UnpackManifest(eastl::span<const uint8_t> buffer);
    static eastl::vector<uint8_t> Pack(const AssetPakMetadata& meta, eastl::span<const uint8_t> data_buffer);
};

} // namespace tryengine::resources