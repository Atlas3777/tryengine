#pragma once

#include <EASTL/string_view.h>

#include "editor/import/IAssetImporter.hpp"
#include "editor/meta/AssetMetaHeader.hpp"
#include "editor/shader/ShaderCompilerSystem.hpp"

namespace tryeditor {

struct SlangImportSettings {};

//! У шейдера нет настроек импорта: блок `settings` в .meta всегда пуст, но остаётся, чтобы
//! все .meta читались одним кодом.
template <>
struct MetaSettingsCodec<SlangImportSettings> {
    static constexpr const char* kTypeName = "SlangImportSettings";

    static Result<SlangImportSettings> Read(const tryengine::resources::ConfigObject&) {
        return SlangImportSettings{};
    }

    static void Write(tryengine::resources::ConfigObject&, const SlangImportSettings&) {}
};

// Импортёр .slang-шейдеров. Всю работу с компилятором (сессии, модули, рефлексия) делает
// ShaderCompilerSystem; здесь остаются только мета и артефакты.
//
// Импортируются только шейдеры с обеими точками входа (vsMain/fsMain и фолбэки). Общий код
// шейдеров — это обычные модули в shader_modules/: у них нет .meta, guid и артефакта, их
// подключает Slang через `import <module>;` при компиляции самого шейдера.
class SlangImporter : public IAssetImporter {
public:
    explicit SlangImporter(ShaderCompilerSystem& compiler) : compiler_(compiler) {}

    [[nodiscard]] constexpr eastl::string_view GetName() const override {
        return importer_name_;
    }

    // Поднимай при изменении того, что попадает в артефакты: все шейдеры переимпортируются.
    [[nodiscard]] uint32_t GetVersion() const override {
        return 4;
    }

    [[nodiscard]] tryengine::async::Task<ImportResult> Import(ImportContext ctx) const override;

    [[nodiscard]] bool ValidateMeta(eastl::span<const uint8_t> meta_bytes) const override {
        return ValidateAssetMeta<SlangImportSettings>(meta_bytes);
    }

private:
    AssetMeta<SlangImportSettings> GenerateDefaultMeta() const;

    ShaderCompilerSystem& compiler_;
    const char* importer_name_ = "SlangImporter";
};

}  // namespace tryeditor
