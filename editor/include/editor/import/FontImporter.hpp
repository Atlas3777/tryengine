#pragma once

#include "editor/import/FontBaker.hpp"
#include "editor/import/IAssetImporter.hpp"
#include "editor/meta/AssetMetaHeader.hpp"

namespace tryeditor {

// .ttf -> distance-field atlas artifact. The importer owns no state: everything it needs
// (bake size, charset, atlas size) lives in the asset's .meta, so a font can be re-baked by
// editing its settings and letting the import stamp force a re-import.
class FontImporter : public IAssetImporter {
public:
    [[nodiscard]] eastl::string_view GetName() const override {
        return importer_name_;
    }

    // Bump when the baked artifact changes for the same source and settings, so every font
    // is re-imported once.
    [[nodiscard]] uint32_t GetVersion() const override {
        return 1;
    }

    [[nodiscard]] tryengine::async::Task<ImportResult> Import(ImportContext ctx) const override;

    [[nodiscard]] bool ValidateMeta(eastl::span<const uint8_t> meta_bytes) const override {
        return ValidateAssetMeta<FontBakeSettings>(meta_bytes);
    }

private:
    [[nodiscard]] AssetMeta<FontBakeSettings> GenerateDefaultMeta() const;

    const char* importer_name_ = "FontImporter";
};

}  // namespace tryeditor
