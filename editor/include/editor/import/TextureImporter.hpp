// #pragma once
//
// #include <filesystem>
//
// #include "BaseTypedImporter.hpp"
// #include "editor/import/IAssetImporter.hpp"
// #include "engine/resources/Types.hpp"
//
// namespace tryeditor {
// struct TextureImportSettings {
//     tryengine::resources::TextureFilter min_filter = tryengine::resources::TextureFilter::Linear;
//     tryengine::resources::TextureFilter mag_filter = tryengine::resources::TextureFilter::Linear;
//     tryengine::resources::TextureAddressMode address_u = tryengine::resources::TextureAddressMode::Repeat;
//     tryengine::resources::TextureAddressMode address_v = tryengine::resources::TextureAddressMode::Repeat;
// };
//
// class TextureImporter : public BaseTypedImporter<TextureImportSettings> {
// public:
//     [[nodiscard]] std::string GetName() const override { return "TextureImporter"; };
//     [[nodiscard]] std::string GetAssetType() const override { return "texture"; };
//
//     bool GenerateArtifact(const AssetContext& context, AssetMetaHeader& header, const TextureImportSettings& settings) override;
//
// };
// }  // namespace tryeditor