#pragma once

#include "editor/import/IAssetImporter.hpp"
#include "editor/import/TextureBaker.hpp"
#include "editor/meta/AssetMetaHeader.hpp"

namespace tryeditor {

// .png/.jpg/.jpeg -> one runtime TextureBinary artifact under the asset guid. The importer owns
// no state: format, mips, size clamp and sampler live in the asset's .meta, so editing them and
// letting the import stamp force a re-import is the whole re-bake workflow.
class TextureImporter : public IAssetImporter {
public:
    [[nodiscard]] eastl::string_view GetName() const override {
        return importer_name_;
    }

    // Bump when the baked artifact changes for the same source and settings, so every texture
    // is re-imported once.
    [[nodiscard]] uint32_t GetVersion() const override {
        return 1;
    }

    [[nodiscard]] tryengine::async::Task<ImportResult> Import(ImportContext ctx) const override;

private:
    [[nodiscard]] AssetMeta<TextureImportSettings> GenerateDefaultMeta() const;

    const char* importer_name_ = "TextureImporter";
};

}  // namespace tryeditor
