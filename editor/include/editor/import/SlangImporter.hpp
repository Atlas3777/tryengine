#pragma once

#include <EASTL/string_view.h>

#include "editor/import/IAssetImporter.hpp"
#include "editor/meta/AssetMetaHeader.hpp"
#include "editor/shader/ShaderCompilerSystem.hpp"

namespace tryeditor {

struct SlangImportSettings {};

// Импортёр .slang-ассетов. Всю работу с компилятором (долгоживущие сессии, рефлексия,
// модули) делает ShaderCompilerSystem; здесь остаются только мета и артефакты.
//
// Один и тот же импортёр обслуживает два вида ассетов:
//   * шейдер — есть вершинная и фрагментная точки входа (vsMain/fsMain и фолбэки);
//   * модуль — точек входа нет; результат импорта — сериализованный Slang-модуль,
//     который подключается к другим шейдерам через `import <ModuleName>;`.
class SlangImporter : public IAssetImporter {
public:
    explicit SlangImporter(ShaderCompilerSystem& compiler) : compiler_(compiler) {}

    [[nodiscard]] constexpr eastl::string_view GetName() const override {
        return importer_name_;
    }

    // Поднимай при изменении того, что попадает в артефакты: все шейдеры переимпортируются.
    [[nodiscard]] uint32_t GetVersion() const override {
        return 2;
    }

    [[nodiscard]] tryengine::async::Task<ImportResult> Import(ImportContext ctx) const override;

private:
    AssetMeta<SlangImportSettings> GenerateDefaultMeta() const;

    ShaderCompilerSystem& compiler_;
    const char* importer_name_ = "SlangImporter";
};

}  // namespace tryeditor
