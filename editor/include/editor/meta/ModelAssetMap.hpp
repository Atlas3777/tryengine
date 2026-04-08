#pragma once
#include <cereal/cereal.hpp>
#include <string>
#include <vector>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/utility.hpp>
#include "engine/core/Components.hpp"

namespace tryeditor {

struct ModelNodeData {
    std::string name;
    tryengine::Transform local_transform;
    std::vector<int32_t> children_indices;

    uint64_t mesh_id = 0;
    uint64_t material_id = 0;

    template <class Archive>
    void serialize(Archive& archive) {
        archive(cereal::make_nvp("name", name), cereal::make_nvp("transform", local_transform),
                cereal::make_nvp("children", children_indices), cereal::make_nvp("mesh_id", mesh_id),
                cereal::make_nvp("material_id", material_id));
    }
};

struct ModelAssetMap {
    uint64_t main_guid = 0;
    std::vector<std::pair<uint64_t, std::string>> sub_assets;

    std::vector<int32_t> scene_roots;
    std::vector<ModelNodeData> nodes;

    template <class Archive>
    void serialize(Archive& archive) {
        archive(cereal::make_nvp("main_guid", main_guid), cereal::make_nvp("sub_assets", sub_assets),
                cereal::make_nvp("scene_roots", scene_roots), cereal::make_nvp("nodes", nodes));
    }
};

}  // namespace tryeditor