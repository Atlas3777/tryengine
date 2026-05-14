#pragma once
#include <filesystem>

#include "editor/import/IAssetImporter.hpp"
#include "engine/resources/Types.hpp"

namespace tryeditor {
struct TextureImportSettings {
    tryengine::resources::TextureFilter min_filter = tryengine::resources::TextureFilter::Linear;
    tryengine::resources::TextureFilter mag_filter = tryengine::resources::TextureFilter::Linear;
    tryengine::resources::TextureAddressMode address_u = tryengine::resources::TextureAddressMode::Repeat;
    tryengine::resources::TextureAddressMode address_v = tryengine::resources::TextureAddressMode::Repeat;

    template <class Archive>
    void serialize(Archive& archive) {
        archive(cereal::make_nvp("min_filter", min_filter));
        archive(cereal::make_nvp("mag_filter", mag_filter));
        archive(cereal::make_nvp("address_u", address_u));
        archive(cereal::make_nvp("address_v", address_v));
    }
};

class TextureImporter : public BaseTypedImporter<TextureImportSettings> {
public:
    [[nodiscard]] std::string GetName() const override { return "TextureImporter"; };
    [[nodiscard]] std::string GetAssetType() const override { return "texture"; };

    bool GenerateArtifact(const AssetContext& context, AssetMetaHeader& header, const TextureImportSettings& settings) override;

};
}  // namespace tryeditor