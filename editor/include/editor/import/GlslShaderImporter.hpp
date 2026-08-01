#pragma once

#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <shaderc/shaderc.hpp>

#include "BaseTypedImporter.hpp"
#include "editor/JsonParser.hpp"
#include "editor/import/IAssetImporter.hpp"
#include "engine/core/FormatUtils.h"

namespace tryeditor {

struct GlslShaderImportSettings {
    shaderc_shader_kind default_stage = shaderc_glsl_infer_from_source;
};

class GlslShaderImporter : public IAssetImporter {
public:
    [[nodiscard]] eastl::string_view GetName() const override { return importer_name_; }

    [[nodiscard]] tryengine::async::Task<ImportResult> Import(ImportContext ctx) const override {
        ImportResult result{};

        if (ctx.source_bytes.empty()) {
            co_return tryengine::core::Error("GLSL source is empty");
        }

        AssetMeta<GlslShaderImportSettings> meta;

        if (!ctx.meta_bytes.empty()) {
            auto parsed_meta = Deserialize<AssetMeta<GlslShaderImportSettings>>(ctx.meta_bytes);
            if (parsed_meta.has_value()) {
                meta = parsed_meta.value();
            }
            else {
                meta = GenerateDefaultMeta();
            }

        } else {
            meta = GenerateDefaultMeta();
        }

        shaderc::Compiler compiler;
        shaderc::CompileOptions options;

        options.SetOptimizationLevel(shaderc_optimization_level_performance);
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);

        const char* source_ptr = ctx.source_bytes.data();
        const size_t source_size = ctx.source_bytes.size();

        shaderc::SpvCompilationResult compilation_result =
            compiler.CompileGlslToSpv(source_ptr, source_size, shaderc_glsl_infer_from_source, "shader_name", options);

        if (compilation_result.GetCompilationStatus() != shaderc_compilation_status_success) {
            TRY_LOG_CRIT("GLSL Compilation error{}", compilation_result.GetErrorMessage());
            co_return tryengine::core::Error(compilation_result.GetErrorMessage());
        };

        ProducedArtifact artifact{};
        artifact.sub_guid = 0;
        artifact.target = ArtifactTarget::Runtime;
        artifact.extension = ".spv";

        // SPIR-V возвращается массив 32-битных слов (uint32_t), копируем их как байты
        const auto* spirv_begin = reinterpret_cast<const char*>(compilation_result.cbegin());
        const auto* spirv_end = reinterpret_cast<const char*>(compilation_result.cend());

        artifact.bytes.assign(spirv_begin, spirv_end);
        result.artifacts.push_back(std::move(artifact));
        result.main_guid = meta.header.guid;

        auto meta_bytes = Serialize<AssetMeta<GlslShaderImportSettings>>(meta);
        if (meta_bytes.has_value()) {
            result.meta_bytes = eastl::move(meta_bytes.value());
        }
        else {
            TRY_LOG_INFO("Что за хрень, не смогли сереализовать мету");
        }

        co_return result;
    }

private:
    AssetMeta<GlslShaderImportSettings> GenerateDefaultMeta() const {
        TRY_LOG_INFO("GENERATE_META");
        AssetMeta<GlslShaderImportSettings> result;
        result.header.importer_type = GetName().data();
        result.header.guid = tryengine::core::RandomUtil::GenerateInt64();
        result.settings = GlslShaderImportSettings{};
        return result;
    }

    const char* importer_name_ = "GlslShaderImporter";
};

}  // namespace tryeditor