#include "editor/import/TextureImporter.hpp"

#include "engine/core/MakeError.hpp"
#include "engine/core/Random.hpp"

namespace tryeditor {

using tryengine::core::LogAndMakeError;

AssetMeta<TextureImportSettings> TextureImporter::GenerateDefaultMeta() const {
    AssetMeta<TextureImportSettings> result;
    result.header.importer_type = GetName().data();
    result.header.guid = tryengine::core::random::GenerateInt64();
    return result;
}

tryengine::async::Task<ImportResult> TextureImporter::Import(ImportContext ctx) const {
    ImportResult result{};

    if (ctx.source_bytes.empty())
        co_return LogAndMakeError("Texture source is empty");

    AssetMeta<TextureImportSettings> meta;
    if (!ctx.meta_bytes.empty()) {
        auto parsed_meta = ParseAssetMeta<TextureImportSettings>(ctx.meta_bytes);
        if (parsed_meta.has_value()) {
            meta = eastl::move(*parsed_meta);
        } else {
            // A typo in the settings must not orphan the asset: keep the guid from the header and
            // bake with defaults, so the texture stays addressable and the log names the problem.
            auto header = ParseMetaHeader(ctx.meta_bytes);
            meta.header = header.has_value() ? eastl::move(*header) : GenerateDefaultMeta().header;
            LogError(LogCategory::Importer, "Texture '{}': .meta settings are invalid; using defaults",
                     eastl::string(ctx.path.data(), ctx.path.size()).c_str());
        }
    } else {
        meta = GenerateDefaultMeta();
    }

    auto baked = BakeTexture(ctx.source_bytes, meta.settings);
    if (!baked.has_value())
        co_return eastl::move(baked.error());

    const TextureBakeStats& stats = baked->stats;
    LogInfo(LogCategory::Importer, "Texture '{}': {}x{}, {} mip(s), format {}, {} bytes",
            eastl::string(ctx.path.data(), ctx.path.size()).c_str(), stats.width, stats.height, stats.mip_count,
            stats.format, stats.bytes);

    // One runtime artifact under the asset's main guid (an empty sub_assets list in the meta
    // means the runtime resource id is the guid itself), like the font and shader artifacts.
    ProducedArtifact artifact{};
    artifact.sub_guid = 0;
    artifact.target = ArtifactTarget::Runtime;
    artifact.extension = ".tex";
    artifact.bytes = eastl::move(baked->bytes);
    result.artifacts.push_back(eastl::move(artifact));

    result.main_guid = meta.header.guid;

    result.meta_bytes = WriteAssetMeta(meta);

    co_return result;
}

}  // namespace tryeditor
