#pragma once
#include <cereal/archives/json.hpp>
#include <fstream>

#include "Paths.hpp"
#include "engine/core/Addressables.hpp"
#include "engine/core/AddressablesGroupAsset.hpp"

namespace tryeditor {

class AddressablesProvider {
public:
    AddressablesProvider(tryengine::core::Addressables& addressables) : addressables_(addressables) {};

    void AddGroup(const tryengine::core::AddressablesGroupAsset& group) {
        CreateGroupFile(group);

        tryengine::core::AddressablesManifestAsset manifest = Load();
        manifest.asset_groups.push_back(group.name);
        Save(manifest);
        addressables_.Refresh();
    }
    [[nodiscard]] tryengine::core::Addressables& GetAddressables() const { return addressables_; }

    void SaveGroup(const tryengine::core::AddressablesGroupAsset& group) {
        auto asset_groups_dir = Paths::project_data_dir / "addressables" / "asset_groups";
        std::ofstream os(asset_groups_dir / (group.name + ".asset_group"));
        cereal::JSONOutputArchive archive(os);
        archive(group);
    }

    void DeleteGroup(const std::string& group_name) {
        // 1. Удаляем из манифеста
        auto manifest = Load();
        auto it = std::find(manifest.asset_groups.begin(), manifest.asset_groups.end(), group_name);
        if (it != manifest.asset_groups.end()) {
            manifest.asset_groups.erase(it);
            Save(manifest);
        }

        // 2. Удаляем физический файл
        auto file_path = Paths::project_data_dir / "addressables" / "asset_groups" / (group_name + ".asset_group");
        if (std::filesystem::exists(file_path)) {
            std::filesystem::remove(file_path);
        }

        // 3. Обновляем данные в памяти
        addressables_.Refresh();
    }

private:
    void CreateGroupFile(const tryengine::core::AddressablesGroupAsset& group) {
        auto asset_groups_dir = Paths::project_data_dir / "addressables" / "asset_groups";

        std::ofstream os(asset_groups_dir / (group.name + ".asset_group"));
        cereal::JSONOutputArchive archive(os);
        archive(group);
    }

    tryengine::core::AddressablesManifestAsset Load() {
        tryengine::core::AddressablesManifestAsset asset;
        std::ifstream is(Paths::project_data_dir / "addressables" / "addressables.addressables");
        cereal::JSONInputArchive archive(is);
        archive(asset);
        return asset;
    }

    void Save(const tryengine::core::AddressablesManifestAsset& manifest) {
        std::ofstream os(Paths::project_data_dir / "addressables" / "addressables.addressables");
        cereal::JSONOutputArchive archive(os);
        archive(manifest);
    }
    tryengine::core::Addressables& addressables_;
};

}  // namespace tryeditor