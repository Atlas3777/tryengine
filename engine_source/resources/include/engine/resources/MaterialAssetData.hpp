#pragma once

#include <array>
#include <cereal/cereal.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/unordered_map.hpp>
#include <map>
#include <string>
#include <unordered_map>

namespace tryengine::resources {

struct MaterialAssetData {
    std::string name;
    uint64_t shader_asset_id;
    std::map<std::string, std::vector<float>> scalar_params;

    std::map<std::string, uint64_t> texture_params;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("shader_id", shader_asset_id),
           cereal::make_nvp("params", scalar_params),
           cereal::make_nvp("textures", texture_params));
    }
};

}  // namespace tryengine::resources