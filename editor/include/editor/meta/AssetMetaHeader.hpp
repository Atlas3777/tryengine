#pragma once
#include <cereal/cereal.hpp>
#include <cereal/types/vector.hpp>
#include <string>

namespace tryeditor {
struct AssetMetaHeader {
    uint64_t guid = 0;
    std::string importer_type;
    std::string asset_type;
    std::vector<uint64_t> sub_assets;

    template <class Archive>
    void serialize(Archive& archive) {
        archive(cereal::make_nvp("guid", guid));
        archive(cereal::make_nvp("importer_type", importer_type));
        archive(cereal::make_nvp("asset_type", asset_type));
        archive(cereal::make_nvp("sub_assets", sub_assets));
    }
};
}  // namespace tryeditor