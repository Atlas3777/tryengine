#pragma once

#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include "editor/meta/AssetMetaHeader.hpp"
#include "engine/core/Result.hpp"

namespace tryeditor {

// .png/.jpg -> TextureBinary artifact.
//
// Settings live in the asset's .meta, a ConfigFormat document the user edits in Import Settings.
// The enum-like fields are strings on purpose: the only place the names are mapped is this file,
// so a typo is an import error instead of a silently different texture. Supported values:
//   format : rgba8, rgba8_srgb, r8, rg8, bc1, bc1_srgb, bc3, bc3_srgb, bc7, bc7_srgb
//   filter : linear, nearest
//   wrap   : repeat, clamp, mirror
struct TextureImportSettings {
    eastl::string format = "rgba8_srgb";
    bool generate_mips = true;
    //! Clamp the longest side before mips are generated; 0 keeps the source size.
    uint32_t max_size = 0;
    eastl::string filter = "linear";
    eastl::string wrap = "repeat";
};

//! Поля блока `settings` в .meta текстуры. Зеркало для редактора — `TextureImportSettings`
//! в editor/daslang/ImportSettings.das: имена полей обязаны совпадать.
template <>
struct MetaSettingsCodec<TextureImportSettings> {
    static constexpr const char* kTypeName = "TextureImportSettings";

    static Result<TextureImportSettings> Read(const tryengine::resources::ConfigObject& object) {
        TextureImportSettings settings;
        auto format = meta::ReadStringField(object, "settings", "format", settings.format);
        if (!format.has_value())
            return eastl::move(format.error());
        settings.format = eastl::move(*format);
        auto mips = meta::ReadBoolField(object, "settings", "generate_mips", settings.generate_mips);
        if (!mips.has_value())
            return eastl::move(mips.error());
        settings.generate_mips = *mips;
        auto max_size = meta::ReadU32Field(object, "settings", "max_size", settings.max_size);
        if (!max_size.has_value())
            return eastl::move(max_size.error());
        settings.max_size = *max_size;
        auto filter = meta::ReadStringField(object, "settings", "filter", settings.filter);
        if (!filter.has_value())
            return eastl::move(filter.error());
        settings.filter = eastl::move(*filter);
        auto wrap = meta::ReadStringField(object, "settings", "wrap", settings.wrap);
        if (!wrap.has_value())
            return eastl::move(wrap.error());
        settings.wrap = eastl::move(*wrap);
        return settings;
    }

    static void Write(tryengine::resources::ConfigObject& object, const TextureImportSettings& settings) {
        object.fields.push_back(meta::MakeStringField("format", settings.format));
        object.fields.push_back(meta::MakeBoolField("generate_mips", settings.generate_mips));
        object.fields.push_back(meta::MakeU32Field("max_size", settings.max_size));
        object.fields.push_back(meta::MakeStringField("filter", settings.filter));
        object.fields.push_back(meta::MakeStringField("wrap", settings.wrap));
    }
};

struct TextureBakeStats {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t mip_count = 1;
    //! tryengine::resources::TextureFormat value, for logs.
    uint32_t format = 0;
    uint64_t bytes = 0;
};

struct TextureBakeOutput {
    eastl::vector<uint8_t> bytes;
    TextureBakeStats stats;
};

//! Decodes an encoded image (stb_image) and writes a TextureBinary artifact. Pure: no IO, no
//! async. The importer and tools/texture_probe.cpp share this exact path, so what the probe
//! validates is what the editor writes.
tryengine::Result<TextureBakeOutput> BakeTexture(eastl::span<const uint8_t> encoded_bytes,
                                                 const TextureImportSettings& settings);

}  // namespace tryeditor
