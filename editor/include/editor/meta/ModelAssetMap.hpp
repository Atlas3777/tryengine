#pragma once

#include <string>
#include <vector>

#include "engine/core/Components.hpp"

namespace tryeditor {

struct ModelNodeData {
    std::string name;
    tryengine::Transform local_transform;
    std::vector<int32_t> children_indices;

    uint64_t mesh_id = 0;
    uint64_t material_id = 0;
};

struct ModelAssetMap {
    uint64_t main_guid = 0;
    std::vector<std::pair<uint64_t, std::string>> sub_assets;

    std::vector<int32_t> scene_roots;
    std::vector<ModelNodeData> nodes;
};

}  // namespace tryeditor