// // editor/include/editor/import/BaseTypedImporter.hpp
// #pragma once
//
// #include "editor/import/IAssetImporter.hpp"
// #include "editor/meta/AssetMetaHeader.hpp"
// #include "engine/resources/JsonSerializer.hpp"
//
// namespace tryeditor {
//
// template <typename TSettings>
// struct AssetMetaFile {
//     AssetMetaHeader header;
//     TSettings settings;
// };
//
// template <typename TSettings>
// class BaseTypedImporter : public IAssetImporter, public ITypedImporter<TSettings>
// {
// public:
//     tryengine::async::Task<void> Reimport(
//         const eastl::string_view asset_path,
//         const eastl::string_view meta_path,
//         const eastl::string_view artifact_dir) override
//     {
//         // 1. Читаем мету по прямому пути
//         auto read_result = co_await fs.ReadText(meta_path);
//         if (!read_result)
//             co_return std::unexpected(read_result.error());
//
//         // 2. Десериализуем через наш JsonSerializer
//         auto meta_result = tryengine::resources::JsonSerializer::Deserialize<AssetMetaFile<TSettings>>(read_result.value());
//         if (!meta_result)
//             co_return std::unexpected(meta_result.error());
//
//         auto& meta_file = meta_result.value();
//         meta_file.header.sub_assets.clear();
//
//         // 3. Передаем управление в генерацию артефактов
//         co_return co_await this->GenerateArtifact(fs, asset_path, artifact_dir, meta_file.header, meta_file.settings);
//     }
//
//     tryengine::async::Task<void> ImportNew(
//         eastl::string_view asset_path,
//         eastl::string_view meta_path,
//         eastl::string_view artifact_dir,
//         uint64_t new_guid) override
//     {
//         TSettings settings{};
//         AssetMetaFile<TSettings> meta_file{
//             .header = {
//                 .guid = new_guid,
//                 .importer_type = this->GetName(),
//                 .asset_type = this->GetAssetType()
//             },
//             .settings = settings
//         };
//
//         // 1. Сериализуем настройки по умолчанию
//         auto serialize_result = tryengine::resources::JsonSerializer::Serialize(meta_file);
//         if (!serialize_result)
//         {
//             co_return std::unexpected(serialize_result.error());
//         }
//
//         // 2. Записываем файл метаданных асинхронно
//         auto write_result = co_await fs.WriteText(meta_path, std::move(serialize_result.value()));
//         if (!write_result)
//         {
//             co_return std::unexpected(write_result.error());
//         }
//
//         co_return co_await this->GenerateArtifact(fs, asset_path, artifact_dir, meta_file.header, meta_file.settings);
//     }
// };
//
// } // namespace tryeditor