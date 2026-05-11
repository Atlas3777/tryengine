#pragma once

#include <filesystem>
#include <fstream>
#include <iostream>

#include "Paths.hpp"
#include "cereal/archives/json.hpp"
#include "engine/core/Addressables.hpp"

namespace tryeditor {
class AppBootstrap {
public:
    static void CheckBaseProjectData() {
        const auto path = std::filesystem::current_path();
        auto addressables_dir = path / "game" / "project_data" / "addressables";
        std::filesystem::create_directories(addressables_dir);
        auto addressables = addressables_dir / "addressables.addressables";

        if (std::filesystem::is_regular_file(addressables)) {
            std::cout << "[Bootstrap]: Addressables find!\n";
        } else {
            std::cerr << "[Bootstrap]: Addressables manifest not found\n";
            const tryengine::core::AddressablesManifestAsset asset;
            SaveProjectData(asset, "addressables/addressables.addressables");
        }

        std::filesystem::create_directory(addressables_dir / "asset_groups");
    }

private:
    template <typename AssetData>
    AssetData LoadProjectData(std::string_view filename) {
        auto path = Paths::project_data_dir;
        path /= filename;

        AssetData data;
        {
            std::ifstream is(path);
            cereal::JSONInputArchive archive(is);
            archive( data);
        }
        return data;
    }

    template <typename AssetData>
    static void SaveProjectData(AssetData data, std::string_view filename) {
        auto path = Paths::project_data_dir;
        path /= filename;

        {
            std::ofstream os(path);
            cereal::JSONOutputArchive archive(os);
            archive(data);
        }
    }
};
}  // namespace tryeditor
