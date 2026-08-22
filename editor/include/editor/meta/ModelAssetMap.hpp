#pragma once

#include "engine/graphics/Transform.hpp"

namespace tryeditor {

struct ModelNodeData {
    eastl::string name;
    tryengine::Transform local_transform;
    eastl::vector<int32_t> children_indices;

    uint64_t mesh_id = 0;
    uint64_t material_id = 0;
};

struct ModelAssetMap {
    uint64_t main_guid = 0;
    eastl::vector<eastl::pair<uint64_t, eastl::string>> sub_assets;

    eastl::vector<int32_t> scene_roots;
    eastl::vector<ModelNodeData> nodes;
};

}  // namespace tryeditor