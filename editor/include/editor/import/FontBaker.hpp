#pragma once

#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include "editor/meta/AssetMetaHeader.hpp"
#include "engine/core/Result.hpp"
#include "engine/resources/binary/FontBinaryLayout.hpp"

namespace tryeditor {

// How a .ttf asset is baked into a distance-field atlas. Stored in the asset's .meta, so a
// font can be re-imported with a different charset or bake size without touching code.
struct FontBakeSettings {
    float em_size = 40.0f;  // bake height in pixels; every metric is in these units
    float px_range = 4.0f;  // distance-field spread in atlas pixels
    uint32_t padding = 4;   // bitmap padding around each glyph (>= px_range)
    uint32_t atlas_width = 1024;
    uint32_t atlas_height = 1024;
    float pixel_dist_scale = 32.0f;  // onedge_value / px_range; see stbtt_GetGlyphSDF
    uint8_t onedge_value = 128;
    // Codepoint ranges separated by commas: "0x20-0x7E,0x400-0x4FF,0x20AC".
    eastl::string ranges = "0x20-0x7E,0xA0-0xFF,0x400-0x4FF,0x2010-0x2027,0x2030,0x2039,0x203A,0x20AC";
};

//! Поля блока `settings` в .meta шрифта. Зеркало для редактора — `FontBakeSettings`
//! в editor/daslang/ImportSettings.das: имена полей обязаны совпадать.
template <>
struct MetaSettingsCodec<FontBakeSettings> {
    static constexpr const char* kTypeName = "FontBakeSettings";

    static Result<FontBakeSettings> Read(const tryengine::resources::ConfigObject& object) {
        FontBakeSettings settings;
        auto em_size = meta::ReadFloatField(object, "settings", "em_size", settings.em_size);
        if (!em_size.has_value())
            return eastl::move(em_size.error());
        settings.em_size = *em_size;
        auto px_range = meta::ReadFloatField(object, "settings", "px_range", settings.px_range);
        if (!px_range.has_value())
            return eastl::move(px_range.error());
        settings.px_range = *px_range;
        auto padding = meta::ReadU32Field(object, "settings", "padding", settings.padding);
        if (!padding.has_value())
            return eastl::move(padding.error());
        settings.padding = *padding;
        auto atlas_width = meta::ReadU32Field(object, "settings", "atlas_width", settings.atlas_width);
        if (!atlas_width.has_value())
            return eastl::move(atlas_width.error());
        settings.atlas_width = *atlas_width;
        auto atlas_height = meta::ReadU32Field(object, "settings", "atlas_height", settings.atlas_height);
        if (!atlas_height.has_value())
            return eastl::move(atlas_height.error());
        settings.atlas_height = *atlas_height;
        auto dist_scale = meta::ReadFloatField(object, "settings", "pixel_dist_scale", settings.pixel_dist_scale);
        if (!dist_scale.has_value())
            return eastl::move(dist_scale.error());
        settings.pixel_dist_scale = *dist_scale;
        auto onedge = meta::ReadU8Field(object, "settings", "onedge_value", settings.onedge_value);
        if (!onedge.has_value())
            return eastl::move(onedge.error());
        settings.onedge_value = *onedge;
        auto ranges = meta::ReadStringField(object, "settings", "ranges", settings.ranges);
        if (!ranges.has_value())
            return eastl::move(ranges.error());
        settings.ranges = eastl::move(*ranges);
        return settings;
    }

    static void Write(tryengine::resources::ConfigObject& object, const FontBakeSettings& settings) {
        object.fields.push_back(meta::MakeFloatField("em_size", settings.em_size));
        object.fields.push_back(meta::MakeFloatField("px_range", settings.px_range));
        object.fields.push_back(meta::MakeU32Field("padding", settings.padding));
        object.fields.push_back(meta::MakeU32Field("atlas_width", settings.atlas_width));
        object.fields.push_back(meta::MakeU32Field("atlas_height", settings.atlas_height));
        object.fields.push_back(meta::MakeFloatField("pixel_dist_scale", settings.pixel_dist_scale));
        object.fields.push_back(meta::MakeU8Field("onedge_value", settings.onedge_value));
        object.fields.push_back(meta::MakeStringField("ranges", settings.ranges));
    }
};

struct FontBakeStats {
    uint32_t codepoints = 0;      // requested in `ranges`
    uint32_t glyphs = 0;          // baked with an outline
    uint32_t empty_glyphs = 0;    // space and other glyphs without an outline (advance only)
    uint32_t missing_glyphs = 0;  // requested codepoints the font does not have
    uint32_t atlas_used_permille = 0;
    float max_advance = 0.0f;
    uint32_t atlas_width = 0;
    uint32_t atlas_height = 0;
};

struct FontBakeOutput {
    eastl::vector<uint8_t> bytes;  // FontBinary blob
    FontBakeStats stats;
};

// Pure bake: TTF bytes + settings in, FontBinary bytes out (no I/O, no engine systems), so the
// result can be checked by tools/font_probe.cpp without launching the editor.
[[nodiscard]] tryengine::Result<FontBakeOutput> BakeFont(eastl::span<const uint8_t> ttf,
                                                         const FontBakeSettings& settings);

}  // namespace tryeditor
