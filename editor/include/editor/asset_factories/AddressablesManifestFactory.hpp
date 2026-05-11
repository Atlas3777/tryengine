// #pragma once
//
// #include <cereal/cereal.hpp>
// #include <cereal/types/vector.hpp>
//
// #include "IAssetFactory.hpp"
// #include "editor/import/ImportSystem.hpp"
// #include "editor/meta/MetaSerializer.hpp"
// #include "engine/core/Addressables.hpp"
// #include "engine/core/AddressablesGroupAsset.hpp"
// #include "engine/core/RandomUtil.hpp"
// #include "engine/resources/Content.hpp"
//
// namespace tryeditor {
//
// class AddressablesManifestFactory : public IAssetFactory {
// public:
//     explicit AddressablesManifestFactory(ImportSystem& import_system) : import_system_(import_system) {};
//
//     uint64_t CreateDefault(const std::filesystem::path& directory) override {
//         tryengine::core::AddressablesManifestAsset asset_group;
//         return Create(directory, "AddresablesManifest.addressables", asset_group);
//     }
//
//     uint64_t Create(const std::filesystem::path& directory, const std::string& name, const tryengine::core::AddressablesManifestAsset& data) {
//         std::string filename = name;
//         if (filename.find(".addressables") == std::string::npos)
//             filename += ".addressables";
//
//         const std::filesystem::path asset_path = directory / filename;
//         // AssetMetaHeader header = CreateMeta(asset_path);
//         // import_system_.SaveNativeAsset(asset_path, data, header);
//
//         // return header.guid;
//     }
//     [[nodiscard]] std::string GetActionName() const override { return "Addressables"; }
//
// private:
//     // AssetMetaHeader CreateMeta(const std::filesystem::path& asset_path) {
//     //     std::filesystem::path meta_path = asset_path.string() + ".meta";
//     //
//     //     AssetMetaHeader header;
//     //     header.guid = tryengine::resources::assets::ADDRESSABLES;
//     //     header.importer_type = "native";
//     //     header.asset_type = "addressables";
//     //
//     //     MetaSerializer::WriteHeaderOnly(meta_path, header);
//     //
//     //     return header;
//     // }
//
//     ImportSystem& import_system_;
// };
//
// }  // namespace tryeditor