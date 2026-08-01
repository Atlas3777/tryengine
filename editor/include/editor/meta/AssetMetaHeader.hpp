#pragma once

#include <EASTL/vector.h>

namespace tryeditor {

struct AssetMetaHeader {
    uint64_t guid = 0;
    eastl::string importer_type;
    eastl::vector<uint64_t> sub_assets;
};

struct HeaderOnly {
    AssetMetaHeader header;
};

template <typename TSettings>
struct AssetMeta {
    AssetMetaHeader header;
    TSettings settings;
};

}  // namespace tryeditor