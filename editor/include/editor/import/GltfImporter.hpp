#pragma once

#include "editor/import/IAssetImporter.hpp"
#include "editor/meta/AssetMetaHeader.hpp"

namespace tryeditor {
struct GltfImportSettings {
    bool extract_materials = false;
};

class GltfImporter : public IAssetImporter {
public:
    [[nodiscard]] eastl::string_view GetName() const override {
        return importer_name_;
    }

    // Поднимай при изменении того, что попадает в артефакты: все модели переимпортируются.
    [[nodiscard]] uint32_t GetVersion() const override {
        // 10: материал меша печётся в шапку артефакта, префаб-артефакт пишет `model`.
        // 11: версия самого префаб-артефакта поднята до 6 (в 10 она осталась 5).
        // 12: `model.material` — строка, а не обёртка `MaterialRef`; версия префаба 7.
        return 12;
    }

    [[nodiscard]] tryengine::async::Task<ImportResult> Import(ImportContext ctx) const override;

private:
    const char* importer_name_ = "GltfImporter";

    // NOTE: was previously declared as `AssetMeta<GltfImportSettings> GltfImporter::GenerateDefaultMeta() const;`
    // A class-scope qualifier (`GltfImporter::`) is not legal on a member declared inside its own
    // class body - that only belongs on the out-of-line definition in the .cpp file.
    [[nodiscard]] AssetMeta<GltfImportSettings> GenerateDefaultMeta() const;
};
}  // namespace tryeditor