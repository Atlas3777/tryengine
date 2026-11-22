#include "editor/import/FontImporter.hpp"

#include "engine/core/MakeError.hpp"
#include "engine/core/Random.hpp"
#include "engine/resources/Json.hpp"

namespace tryeditor {

using tryengine::core::LogAndMakeError;

AssetMeta<FontBakeSettings> FontImporter::GenerateDefaultMeta() const {
    AssetMeta<FontBakeSettings> result;
    result.header.importer_type = GetName().data();
    result.header.guid = tryengine::core::random::GenerateInt64();
    return result;
}

tryengine::async::Task<ImportResult> FontImporter::Import(ImportContext ctx) const {
    ImportResult result{};

    if (ctx.source_bytes.empty())
        co_return LogAndMakeError("Font source is empty");

    AssetMeta<FontBakeSettings> meta;
    if (!ctx.meta_bytes.empty()) {
        auto parsed_meta = tryengine::resources::Json::Deserialize<AssetMeta<FontBakeSettings>>(ctx.meta_bytes);
        meta = parsed_meta.value_or(GenerateDefaultMeta());
    } else {
        meta = GenerateDefaultMeta();
    }

    auto baked = BakeFont(ctx.source_bytes, meta.settings);
    if (!baked.has_value())
        co_return eastl::move(baked.error());

    const FontBakeStats& stats = baked->stats;
    LogInfo(LogCategory::Importer, "Font '{}': {} codepoints, {} glyphs, {} empty, {} missing, atlas {}x{} ({}‰)",
            eastl::string(ctx.path.data(), ctx.path.size()).c_str(), stats.codepoints, stats.glyphs, stats.empty_glyphs,
            stats.missing_glyphs, stats.atlas_width, stats.atlas_height, stats.atlas_used_permille);

    // One runtime artifact under the asset's main guid (an empty sub_assets list in the meta
    // means the runtime resource id is the guid itself), exactly like a compiled shader.
    ProducedArtifact artifact{};
    artifact.sub_guid = 0;
    artifact.target = ArtifactTarget::Runtime;
    artifact.extension = ".font";
    artifact.bytes = eastl::move(baked->bytes);
    result.artifacts.push_back(eastl::move(artifact));

    result.main_guid = meta.header.guid;

    auto meta_bytes = tryengine::resources::Json::Serialize<AssetMeta<FontBakeSettings>>(meta);
    if (!meta_bytes.has_value())
        co_return LogAndMakeError("Failed to serialize font meta");
    result.meta_bytes = eastl::move(meta_bytes.value());

    co_return result;
}

}  // namespace tryeditor
