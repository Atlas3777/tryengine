#pragma once
#include <cereal/cereal.hpp>

namespace tryeditor {
struct AssetHeader {
    uint64_t main_uuid;

    template <class Archive>
    void serialize(Archive& archive) {
        archive(cereal::make_nvp("main_guid", main_uuid));
    }
};
}  // namespace tryengine::resources