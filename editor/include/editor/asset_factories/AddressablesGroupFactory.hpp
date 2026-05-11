// #pragma once
//
// #include "engine/core/Addressables.hpp"
// #include "engine/core/AddressablesGroupAsset.hpp"
// #include "engine/core/RandomUtil.hpp"
// #include "engine/resources/Content.hpp"
//
// namespace tryeditor {
//
// class AddressablesGroupFactory : public IAssetFactory {
// public:
//     explicit AddressablesGroupFactory(ImportSystem& import_system) : import_system_(import_system) {};
//
//     uint64_t CreateDefault(const std::filesystem::path& directory) override {
//         tryengine::core::AddressablesGroupAsset asset_group;
//         asset_group.name = "default_group";
//         return Create(directory, "DefaultGroup.assets_group", asset_group);
//     }
//
//     uint64_t Create(const std::filesystem::path& directory, const std::string& name, const tryengine::core::AddressablesGroupAsset& data) {
//         std::string filename = name;
//         if (filename.find(".assets_group") == std::string::npos)
//             filename += ".assets_group";
//
//         const std::filesystem::path asset_path = directory / filename;
//         AssetMetaHeader header = CreateMeta(asset_path);
//         import_system_.SaveNativeAsset(asset_path, data, header);
//
//
//         tryengine::core::AddressablesManifestAsset addressables_manifest;
//         import_system_.LoadNativeAsset<tryengine::core::AddressablesManifestAsset>(addressables_path, addressables_manifest);
//         addressables_manifest.asset_groups.push_back(data.name);
//         import_system_.SaveNativeAsset(addressables_path, addressables_manifest, header);
//
//         return header.guid;
//     }
//     [[nodiscard]] std::string GetActionName() const override { return "Addressables Group"; }
//
// private:
//     AssetMetaHeader CreateMeta(const std::filesystem::path& asset_path) {
//         std::filesystem::path meta_path = asset_path.string() + ".meta";
//
//         AssetMetaHeader header;
//         header.guid = tryengine::core::RandomUtil::GenerateInt64();
//         header.importer_type = "native";
//         header.asset_type = "asset_group";
//
//         MetaSerializer::WriteHeaderOnly(meta_path, header);
//
//         return header;
//     }
//
//     ImportSystem& import_system_;
// };
//
// }  // namespace tryeditor