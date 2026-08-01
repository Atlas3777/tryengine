#pragma once

#include <map>
#include <string>

namespace tryengine::resources {

struct MaterialAssetData {
    std::string name;
    uint64_t shader_asset_id;
    std::map<std::string, std::vector<float>> scalar_params;

    std::map<std::string, uint64_t> texture_params;

};

}  // namespace tryengine::resources