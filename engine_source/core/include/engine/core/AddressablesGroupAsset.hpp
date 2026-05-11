#pragma once

#include <cereal/cereal.hpp>
#include <cereal/types/unordered_map.hpp>
#include <cereal/types/string.hpp>
#include <string>
#include <unordered_map>

namespace tryengine::core {
struct AddressablesGroupAsset {
    std::string name;
    std::unordered_map<std::string, uint64_t> map;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("group_name", name),
            cereal::make_nvp("asset_map", map));
    }
};
}  // namespace tryengine::resources