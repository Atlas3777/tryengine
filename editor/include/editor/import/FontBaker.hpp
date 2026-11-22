#pragma once

#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

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
