#include "editor/import/SlangImporter.hpp"

#include "engine/core/MakeError.hpp"
#include "engine/core/Random.hpp"
#include "engine/graphics/ShaderBinary.hpp"

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
        auto parsed_meta = ParseAssetMeta<SlangImportSettings>(ctx.meta_bytes);
        if (parsed_meta.has_value()) {
            meta = eastl::move(*parsed_meta);
        } else {
            // Keep the guid: a shader artifact under a new id would not be found by materials.
            auto header = ParseMetaHeader(ctx.meta_bytes);
            meta.header = header.has_value() ? eastl::move(*header) : GenerateDefaultMeta().header;
            LogError(LogCategory::Importer, "Shader '{}': .meta is invalid; using defaults",
                     eastl::string(ctx.path.data(), ctx.path.size()).c_str());
        }
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

    result.dependencies.reserve(compiled.dependencies.size());
    for (const ShaderDependency& dependency : compiled.dependencies)
        result.dependencies.push_back(ImportDependencyRef{dependency.name, dependency.path});

    result.main_guid = meta.header.guid;

    result.meta_bytes = WriteAssetMeta(meta);

    co_return result;
}

}  // namespace tryeditor
