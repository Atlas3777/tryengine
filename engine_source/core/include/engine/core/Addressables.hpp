#pragma once
#include <cereal/archives/json.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/vector.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <vector>

#include "engine/core/AddressablesGroupAsset.hpp"

namespace tryengine::core {

struct AddressablesManifestAsset {
    std::vector<std::string> asset_groups;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("asset_groups", asset_groups));
    }
};

class Addressables {
public:
    void Refresh() {
        addressables_groups_.clear();
        auto proj_dir = std::filesystem::current_path() / "game" / "project_data";

        AddressablesManifestAsset asset;
        {
            std::ifstream is(proj_dir / "addressables" / "addressables.addressables");
            cereal::JSONInputArchive archive(is);
            archive(asset);
        }
        for (auto& asset_group_name : asset.asset_groups) {
            AddressablesGroupAsset asset_group_asset;
            {
                std::ifstream is(proj_dir / "addressables" / "asset_groups" / (asset_group_name + ".asset_group"));
                cereal::JSONInputArchive archive(is);
                archive(asset_group_asset);
            }
            std::cout << "[Addressables]: added asset group " << asset_group_asset.name << std::endl;
            addressables_groups_.push_back(asset_group_asset);
        }
    }

    uint64_t Get(std::string_view address) {
        for (const auto& group : addressables_groups_) {
            auto it = group.map.find(std::string(address));
            if (it != group.map.end()) {
                return it->second;
            }
        }
        std::cerr << "[Addressables]: Could not find " << address << std::endl;
        return 0;
    }

    std::vector<AddressablesGroupAsset>& GetGroups() { return addressables_groups_; }

private:
    std::vector<AddressablesGroupAsset> addressables_groups_;
};
}  // namespace tryengine::core