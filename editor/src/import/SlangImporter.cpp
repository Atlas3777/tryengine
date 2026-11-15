#include "editor/import/SlangImporter.hpp"

#include "engine/core/MakeError.hpp"
#include "engine/core/Random.hpp"
#include "engine/graphics/ShaderBinary.hpp"
#include "engine/resources/Json.hpp"

namespace tryeditor {

using tryengine::core::LogAndMakeError;

AssetMeta<SlangImportSettings> SlangImporter::GenerateDefaultMeta() const {
    AssetMeta<SlangImportSettings> result;
    result.header.importer_type = GetName().data();
    result.header.guid = tryengine::core::random::GenerateInt64();
    return result;
}

tryengine::async::Task<ImportResult> SlangImporter::Import(ImportContext ctx) const {
    ImportResult result{};

    if (ctx.source_bytes.empty())
        co_return LogAndMakeError("Slang source is empty");

    AssetMeta<SlangImportSettings> meta;
    if (!ctx.meta_bytes.empty()) {
        auto parsed_meta = tryengine::resources::Json::Deserialize<AssetMeta<SlangImportSettings>>(ctx.meta_bytes);
        meta = parsed_meta.value_or(GenerateDefaultMeta());
    } else {
        meta = GenerateDefaultMeta();
    }

    ShaderCompileRequest request{};
    request.path = eastl::string(ctx.path.data(), ctx.path.size());
    request.source = eastl::string(reinterpret_cast<const char*>(ctx.source_bytes.data()), ctx.source_bytes.size());

    auto compile_result = compiler_.Compile(request);
    if (!compile_result.has_value())
        co_return eastl::move(compile_result.error());

    ShaderCompileResult& compiled = *compile_result;

    if (compiled.kind == ShaderAssetKind::Shader) {
        eastl::vector<uint8_t> packed_binary = tryengine::graphics::PackShaderBinary(compiled.shader);

        ProducedArtifact debug_artifact{};
        debug_artifact.sub_guid = 88;
        debug_artifact.target = ArtifactTarget::Editor;
        debug_artifact.extension = ".reflection.txt";
        debug_artifact.bytes.assign(
            reinterpret_cast<const uint8_t*>(compiled.reflection_dump.data()),
            reinterpret_cast<const uint8_t*>(compiled.reflection_dump.data()) + compiled.reflection_dump.size());
        result.artifacts.push_back(std::move(debug_artifact));

        ProducedArtifact artifact{};
        artifact.sub_guid = 0;
        artifact.target = ArtifactTarget::Runtime;
        artifact.extension = ".tshd";
        artifact.bytes = std::move(packed_binary);
        result.artifacts.push_back(std::move(artifact));
    } else {
        ProducedArtifact artifact{};
        artifact.sub_guid = 0;
        artifact.target = ArtifactTarget::Runtime;
        artifact.extension = ".slang-module";
        artifact.bytes = std::move(compiled.module_ir);
        result.artifacts.push_back(std::move(artifact));

        LogInfo(LogCategory::Importer, "Slang module '{}' imported", compiled.module_name.c_str());
    }

    result.dependencies.reserve(compiled.dependencies.size());
    for (const ShaderDependency& dependency : compiled.dependencies)
        result.dependencies.push_back(ImportDependencyRef{dependency.name, dependency.path});

    result.main_guid = meta.header.guid;

    auto meta_bytes = tryengine::resources::Json::Serialize<AssetMeta<SlangImportSettings>>(meta);
    if (meta_bytes.has_value())
        result.meta_bytes = eastl::move(meta_bytes.value());
    else
        LogError("Не удалось сериализовать мету");

    co_return result;
}

}  // namespace tryeditor
