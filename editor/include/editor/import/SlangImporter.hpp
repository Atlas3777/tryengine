#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <slang-com-ptr.h>
#include <slang.h>

#include "BaseTypedImporter.hpp"
#include "editor/JsonParser.hpp"
#include "editor/import/IAssetImporter.hpp"
#include "editor/meta/AssetMetaHeader.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/core/Random.hpp"

namespace tryeditor {

struct SlangImportSettings {};

class SlangImporter : public IAssetImporter {
public:
    [[nodiscard]] eastl::string_view GetName() const override { return importer_name_; }

    [[nodiscard]] tryengine::async::Task<ImportResult> Import(ImportContext ctx) const override {
        ImportResult result{};

        if (ctx.source_bytes.empty())
            co_return LogAndMakeError("HLSL source is empty");

        AssetMeta<SlangImportSettings> meta;

        if (!ctx.meta_bytes.empty()) {
            auto parsed_meta = Deserialize<AssetMeta<SlangImportSettings>>(ctx.meta_bytes);
            if (parsed_meta.has_value()) {
                meta = parsed_meta.value();
            } else {
                meta = GenerateDefaultMeta();
            }
        } else {
            meta = GenerateDefaultMeta();
        }

        eastl::string source_code(reinterpret_cast<const char*>(ctx.source_bytes.data()), ctx.source_bytes.size());

        // 1. Создаем глобальную сессию Slang (в продакшене ее лучше кэшировать, а не создавать каждый раз)
        Slang::ComPtr<slang::IGlobalSession> global_session;
        if (SLANG_FAILED(slang::createGlobalSession(global_session.writeRef()))) {
            co_return LogAndMakeError("Slang: Failed to create global session");
        }

        // 2. Настраиваем целевой формат — SPIR-V
        slang::TargetDesc target_desc = {};
        target_desc.format = SLANG_SPIRV;
        target_desc.profile = global_session->findProfile("spirv_1_5");  // Укажите нужную версию SPIR-V

        slang::SessionDesc session_desc = {};
        session_desc.targets = &target_desc;
        session_desc.targetCount = 1;

        Slang::ComPtr<slang::ISession> session;
        if (SLANG_FAILED(global_session->createSession(session_desc, session.writeRef()))) {
            co_return LogAndMakeError("Slang: Failed to create session");
        }

        // 3. Загружаем исходный код модуля из строки
        Slang::ComPtr<slang::IBlob> diagnostics;
        slang::IModule* module =
            session->loadModuleFromSourceString("imported_shader",       // Имя модуля
                                                "ctx.asset_path.c_str()",  // Путь/имя файла для логов ошибок
                                                source_code.c_str(), diagnostics.writeRef());

        if (!module) {
            const char* error_msg =
                diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "Unknown error";
            co_return LogAndMakeError("Slang compilation error:\n{}", error_msg);
        }

        // 4. Находим точку входа (например, "main", "vsMain" или берущуюся из HLSLImportSettings)
        const char* entry_point_name = "main";  // Здесь можно подставить meta.settings.entry_point
        Slang::ComPtr<slang::IEntryPoint> entry_point;
        if (SLANG_FAILED(module->findEntryPointByName(entry_point_name, entry_point.writeRef())) || !entry_point) {
            co_return LogAndMakeError("Slang: Entry point '{}' not found", entry_point_name);
        }

        // 5. Компонуем модуль и точку входа в единую программу
        slang::IComponentType* components[] = {module, entry_point.get()};
        Slang::ComPtr<slang::IComponentType> program;
        if (SLANG_FAILED(
                session->createCompositeComponentType(components, 2, program.writeRef(), diagnostics.writeRef()))) {
            const char* error_msg =
                diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "Link error";
            co_return LogAndMakeError("Slang linking error:\n{}", error_msg);
        }

        // 6. Генерируем SPIR-V байткод
        Slang::ComPtr<slang::IBlob> spirv_blob;
        if (SLANG_FAILED(program->getEntryPointCode(0, 0, spirv_blob.writeRef(), diagnostics.writeRef()))) {
            const char* error_msg =
                diagnostics ? static_cast<const char*>(diagnostics->getBufferPointer()) : "CodeGen error";
            co_return LogAndMakeError("Slang SPIR-V generation error:\n{}", error_msg);
        }

        // 7. Записываем результат в артефакт
        ProducedArtifact artifact{};
        artifact.sub_guid = 0;
        artifact.target = ArtifactTarget::Runtime;
        artifact.extension = ".spv";

        const char* spirv_begin = static_cast<const char*>(spirv_blob->getBufferPointer());
        const char* spirv_end = spirv_begin + spirv_blob->getBufferSize();

        artifact.bytes.assign(spirv_begin, spirv_end);

        result.artifacts.push_back(std::move(artifact));
        result.main_guid = meta.header.guid;

        co_return result;
    }

private:
    AssetMeta<SlangImportSettings> GenerateDefaultMeta() const {
        LogInfo("GENERATE_META");
        AssetMeta<SlangImportSettings> result;
        result.header.importer_type = GetName().data();
        result.header.guid = tryengine::core::random::GenerateInt64();
        result.settings = SlangImportSettings{};
        return result;
    }

    const char* importer_name_ = "SlangImporter";
};

}  // namespace tryeditor