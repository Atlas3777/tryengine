#pragma once

#include "editor/import/GltfUpAxis.hpp"
#include "editor/import/IAssetImporter.hpp"
#include "editor/meta/AssetMetaHeader.hpp"

namespace tryeditor {
struct GltfImportSettings {
    bool extract_materials = false;
    //! Какая ось файла смотрит вверх. Значение — настоящий enum: в .meta он пишется именем
    //! (`Y`/`Z`/`X`), потому что ConfigFormat сериализует enum'ы именем, и это же имя
    //! показывает форма редактора.
    GltfUpAxis up_axis = GltfUpAxis::Y;
};

//! Поля блока `settings` в .meta модели. Зеркало для редактора — `GltfImportSettings`
//! в editor/daslang/ImportSettings.das.
template <>
struct MetaSettingsCodec<GltfImportSettings> {
    static constexpr const char* kTypeName = "GltfImportSettings";

    static Result<GltfImportSettings> Read(const tryengine::resources::ConfigObject& object) {
        GltfImportSettings settings;
        auto extract = meta::ReadBoolField(object, "settings", "extract_materials", settings.extract_materials);
        if (!extract.has_value())
            return eastl::move(extract.error());
        settings.extract_materials = *extract;
        auto up_axis = meta::ReadStringField(object, "settings", "up_axis", GltfUpAxisName(settings.up_axis));
        if (!up_axis.has_value())
            return eastl::move(up_axis.error());
        if (!GltfUpAxisFromName(*up_axis, settings.up_axis))
            return LogAndMakeError(".meta: field 'settings.up_axis' must be Y, Z or X (got '{}')", up_axis->c_str());
        return settings;
    }

    static void Write(tryengine::resources::ConfigObject& object, const GltfImportSettings& settings) {
        object.fields.push_back(meta::MakeBoolField("extract_materials", settings.extract_materials));
        object.fields.push_back(meta::MakeStringField("up_axis", eastl::string(GltfUpAxisName(settings.up_axis))));
    }
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
        // 13: корни модели поворачиваются в Z-up движка по `settings.up_axis`.
        return 13;
    }

    [[nodiscard]] tryengine::async::Task<ImportResult> Import(ImportContext ctx) const override;

    [[nodiscard]] bool ValidateMeta(eastl::span<const uint8_t> meta_bytes) const override {
        return ValidateAssetMeta<GltfImportSettings>(meta_bytes);
    }

private:
    const char* importer_name_ = "GltfImporter";

    // NOTE: was previously declared as `AssetMeta<GltfImportSettings> GltfImporter::GenerateDefaultMeta() const;`
    // A class-scope qualifier (`GltfImporter::`) is not legal on a member declared inside its own
    // class body - that only belongs on the out-of-line definition in the .cpp file.
    [[nodiscard]] AssetMeta<GltfImportSettings> GenerateDefaultMeta() const;
};
}  // namespace tryeditor