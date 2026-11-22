#pragma once

#include <cstdint>

namespace tryengine::resources {

// A baked font: metrics plus a nested TextureBinary with the distance-field atlas. The
// importer writes it, the runtime reads it; nothing in between interprets glyph shapes.
//
// All metric values are in em pixels at `em_size`; the atlas is baked once at that size and
// rendered at any size by scaling. `plane_*` are offsets from the pen position on the
// baseline in the UI's y-down space; `atlas_*` are normalized UVs (0..1, v down).
inline constexpr uint32_t kFontMagic = 0x544E4F46u;  // 'FONT'
inline constexpr uint32_t kFontVersion = 1;

// The field is single-channel SDF today. `mode` keeps the artifact readable if a multi-channel
// field (MSDF) is baked later: the shader branches on it, the format itself does not change.
enum class FontFieldMode : uint32_t {
    Sdf = 0,
    Msdf = 1,
};

#pragma pack(push, 1)

struct FontHeader {
    uint32_t magic = kFontMagic;
    uint32_t version = kFontVersion;
    uint32_t mode = static_cast<uint32_t>(FontFieldMode::Sdf);
    uint32_t glyph_count = 0;
    uint32_t kerning_count = 0;
    uint32_t atlas_blob_size = 0;
    float em_size = 0.0f;
    float px_range = 0.0f;
    float onedge_value = 128.0f;
    float pixel_dist_scale = 1.0f;
    float line_height = 0.0f;
    float ascender = 0.0f;
    float descender = 0.0f;
    uint32_t _pad = 0;
};

struct FontGlyph {
    uint32_t codepoint = 0;
    float advance = 0.0f;
    float plane_left = 0.0f;
    float plane_top = 0.0f;
    float plane_right = 0.0f;
    float plane_bottom = 0.0f;
    float atlas_left = 0.0f;
    float atlas_top = 0.0f;
    float atlas_right = 0.0f;
    float atlas_bottom = 0.0f;
};

struct FontKerning {
    uint32_t first = 0;
    uint32_t second = 0;
    float advance = 0.0f;
};

#pragma pack(pop)

static_assert(sizeof(FontHeader) == 56, "FontHeader layout is part of the artifact format");
static_assert(sizeof(FontGlyph) == 40, "FontGlyph layout is part of the artifact format");
static_assert(sizeof(FontKerning) == 12, "FontKerning layout is part of the artifact format");

}  // namespace tryengine::resources
