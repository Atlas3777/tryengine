#pragma once

#include "editor/import/IAssetImporter.hpp"
#include "editor/meta/AssetMetaHeader.hpp"

namespace tryeditor {
struct GltfImportSettings {
    bool extract_materials = false;
};

class GltfImporter : public IAssetImporter {
public:
    [[nodiscard]] eastl::string_view GetName() const override { return importer_name_; }

    [[nodiscard]] tryengine::async::Task<ImportResult> Import(ImportContext ctx) const override;

private:
    const char* importer_name_ = "GltfImporter";

    // NOTE: was previously declared as `AssetMeta<GltfImportSettings> GltfImporter::GenerateDefaultMeta() const;`
    // A class-scope qualifier (`GltfImporter::`) is not legal on a member declared inside its own
    // class body - that only belongs on the out-of-line definition in the .cpp file.
    [[nodiscard]] AssetMeta<GltfImportSettings> GenerateDefaultMeta() const;
};
}  // namespace tryeditor