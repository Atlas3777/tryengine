// #pragma once
//
// #include "editor/asset_factories/IAssetFactory.hpp"
// #include "editor/import/ImportSystem.hpp"
// #include "engine/resources/Types.hpp"
//
// namespace tryeditor {
//
// class MaterialAssetFactory : public BaseAssetFactory<tryengine::resources::MaterialAsset> {
// public:
//     explicit MaterialAssetFactory(ImportSystem& import_system) : import_system_(import_system) {}
//
//     [[nodiscard]] std::string GetAssetType() const override { return "material"; }
//     [[nodiscard]] std::string GetActionName() const override { return "Material Asset"; }
//     [[nodiscard]] std::string GetExtension() const override { return ".mat"; }
//     [[nodiscard]] std::string GetDefaultName() const override { return "new_material"; };
//     [[nodiscard]] tryengine::resources::MaterialAsset CreateDefaultAsset() const override {
//         tryengine::resources::MaterialAsset a{};
//         return a;
//     }
//
//     uint64_t Create(const tryengine::resources::MaterialAsset& data, const std::filesystem::path& directory,
//                     const std::string& name, uint64_t forced_guid = 0) {
//         std::filesystem::path asset_path = directory / (name + GetExtension());
//
//         // // 1. Сохраняем сам файл ассета (JSON)
//         // SaveSource(asset_path, data);
//
//         // 2. Создаем мета-данные
//         AssetMetaHeader header;
//         header.guid = (forced_guid != 0) ? forced_guid : tryengine::core::random::GenerateInt64();
//         header.importer_type = "NativeImporter";
//         // header.asset_type = GetAssetType();
//
//         // MetaSerializer::Write(asset_path.string() + ".meta", header, EmptySettings{});
//
//         // import_system_.RegisterAndCompileExternalAsset(asset_path, header);
//
//         return header.guid;
//     }
//
//     ImportSystem& import_system_;
// };
//
// }  // namespace tryeditor