#include "editor/import/FontBaker.hpp"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <cmath>
#include <cstring>

#include "engine/core/MakeError.hpp"
#include "engine/resources/binary/FontBinary.hpp"
#include "engine/resources/binary/TextureBinary.hpp"

// The baker is the only translation unit that pulls in the stb implementations: the editor
// gets them through editor_core, and tools/font_probe.cpp compiles this file directly.
#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

namespace tryeditor {

namespace {

using tryengine::resources::FontGlyph;
using tryengine::resources::FontHeader;
using tryengine::resources::TextureBinary;
using tryengine::resources::TextureFormat;
using tryengine::resources::TextureHeader;
using tryengine::resources::TextureSampleCount;
using tryengine::resources::TextureType;
using tryengine::resources::TextureViewType;
using tryengine::resources::VkFilter;
using tryengine::resources::VkSamplerAddressMode;
using tryengine::resources::VkSamplerMipmapMode;

struct CodepointRange {
    uint32_t first = 0;
    uint32_t last = 0;
};

bool ParseHex(const char* begin, const char* end, uint32_t& out) {
    if (begin == end)
        return false;
    uint32_t value = 0;
    for (const char* p = begin; p != end; ++p) {
        const char c = *p;
        uint32_t digit = 0;
        if (c >= '0' && c <= '9')
            digit = static_cast<uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            digit = static_cast<uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            digit = static_cast<uint32_t>(c - 'A' + 10);
        else
            return false;
        value = value * 16u + digit;
    }
    out = value;
    return true;
}

//! "0x20-0x7E,0x400, 0x2010-0x203A" -> merged codepoint ranges. Overlapping ranges are merged
//! so a codepoint is baked once even if the settings describe it twice.
bool ParseRanges(eastl::string_view text, eastl::vector<CodepointRange>& out, eastl::string& error) {
    eastl::vector<CodepointRange> raw;
    size_t pos = 0;
    while (pos <= text.size()) {
        const size_t comma = text.find(',', pos);
        const size_t end = (comma == eastl::string_view::npos) ? text.size() : comma;
        eastl::string_view token = text.substr(pos, end - pos);
        while (!token.empty() && (token.front() == ' ' || token.front() == '\t'))
            token.remove_prefix(1);
        while (!token.empty() && (token.back() == ' ' || token.back() == '\t'))
            token.remove_suffix(1);

        if (!token.empty()) {
            const size_t dash = token.find('-');
            const eastl::string_view first_text = token.substr(0, dash);
            if (!first_text.starts_with("0x") && !first_text.starts_with("0X")) {
                error = "font ranges must be hex codepoints like '0x20-0x7E'";
                return false;
            }
            uint32_t first = 0;
            if (!ParseHex(first_text.data() + 2, first_text.data() + first_text.size(), first)) {
                error = "invalid codepoint in font ranges";
                return false;
            }
            uint32_t last = first;
            if (dash != eastl::string_view::npos) {
                const eastl::string_view last_text = token.substr(dash + 1);
                if (!last_text.starts_with("0x") && !last_text.starts_with("0X")) {
                    error = "font ranges must be hex codepoints like '0x20-0x7E'";
                    return false;
                }
                if (!ParseHex(last_text.data() + 2, last_text.data() + last_text.size(), last)) {
                    error = "invalid codepoint in font ranges";
                    return false;
                }
            }
            if (last < first) {
                error = "font range end is below its start";
                return false;
            }
            if (last > 0x10FFFFu) {
                error = "font range exceeds the Unicode space";
                return false;
            }
            raw.push_back(CodepointRange{.first = first, .last = last});
        }

        if (comma == eastl::string_view::npos)
            break;
        pos = comma + 1;
    }

    if (raw.empty()) {
        error = "font ranges are empty";
        return false;
    }

    eastl::sort(raw.begin(), raw.end(),
                [](const CodepointRange& a, const CodepointRange& b) { return a.first < b.first; });
    for (const CodepointRange& range : raw)
        if (!out.empty() && range.first <= out.back().last)
            out.back().last = eastl::max(out.back().last, range.last);
        else
            out.push_back(range);
    return true;
}

//! One glyph that has an outline and therefore a distance-field bitmap.
struct BakedGlyph {
    uint32_t codepoint = 0;
    float advance = 0.0f;
    int xoff = 0;
    int yoff = 0;
    int width = 0;
    int height = 0;
    eastl::vector<uint8_t> sdf;
};

}  // namespace

tryengine::Result<FontBakeOutput> BakeFont(eastl::span<const uint8_t> ttf, const FontBakeSettings& settings) {
    using tryengine::core::LogAndMakeError;

    if (ttf.empty())
        return LogAndMakeError("BakeFont: font source is empty");
    if (settings.em_size <= 0.0f)
        return LogAndMakeError("BakeFont: em_size must be positive");
    if (settings.atlas_width == 0 || settings.atlas_height == 0)
        return LogAndMakeError("BakeFont: atlas size must be positive");
    if (settings.onedge_value == 0 || settings.onedge_value == 255)
        return LogAndMakeError("BakeFont: onedge_value must leave room below and above the edge");
    if (settings.pixel_dist_scale == 0.0f)
        return LogAndMakeError("BakeFont: pixel_dist_scale must be non-zero");

    eastl::vector<CodepointRange> ranges;
    eastl::string range_error;
    if (!ParseRanges(settings.ranges, ranges, range_error))
        return LogAndMakeError("BakeFont: {}", range_error.c_str());

    // A font collection needs an explicit face index; v1 supports single-face files only.
    const int face_count = stbtt_GetNumberOfFonts(ttf.data());
    if (face_count < 0)
        return LogAndMakeError("BakeFont: not a TrueType font");
    if (face_count > 1)
        return LogAndMakeError("BakeFont: font collections (.ttc) are not supported, export a single .ttf");

    stbtt_fontinfo info{};
    if (!stbtt_InitFont(&info, ttf.data(), stbtt_GetFontOffsetForIndex(ttf.data(), 0)))
        return LogAndMakeError("BakeFont: stbtt_InitFont failed");

    const float scale = stbtt_ScaleForPixelHeight(&info, settings.em_size);
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &line_gap);

    FontBakeOutput output;
    output.stats.atlas_width = settings.atlas_width;
    output.stats.atlas_height = settings.atlas_height;

    eastl::vector<BakedGlyph> baked;
    eastl::vector<FontGlyph> glyphs;

    for (const CodepointRange& range : ranges) {
        for (uint32_t cp = range.first; cp <= range.last; ++cp) {
            ++output.stats.codepoints;

            const int glyph_index = stbtt_FindGlyphIndex(&info, static_cast<int>(cp));
            if (glyph_index == 0) {
                ++output.stats.missing_glyphs;
                continue;
            }

            int advance = 0;
            int left_side_bearing = 0;
            stbtt_GetGlyphHMetrics(&info, glyph_index, &advance, &left_side_bearing);

            FontGlyph glyph{};
            glyph.codepoint = cp;
            glyph.advance = static_cast<float>(advance) * scale;
            output.stats.max_advance = eastl::max(output.stats.max_advance, glyph.advance);

            int width = 0;
            int height = 0;
            int xoff = 0;
            int yoff = 0;
            unsigned char* sdf =
                stbtt_GetGlyphSDF(&info, scale, glyph_index, static_cast<int>(settings.padding), settings.onedge_value,
                                  settings.pixel_dist_scale, &width, &height, &xoff, &yoff);
            if (sdf == nullptr || width <= 0 || height <= 0) {
                // Whitespace and other outline-less glyphs: advance only, no bitmap.
                if (sdf != nullptr)
                    stbtt_FreeSDF(sdf, info.userdata);
                ++output.stats.empty_glyphs;
                glyphs.push_back(glyph);
                continue;
            }

            BakedGlyph entry{};
            entry.codepoint = cp;
            entry.advance = glyph.advance;
            entry.xoff = xoff;
            entry.yoff = yoff;
            entry.width = width;
            entry.height = height;
            entry.sdf.assign(sdf, sdf + static_cast<size_t>(width) * static_cast<size_t>(height));
            stbtt_FreeSDF(sdf, info.userdata);
            baked.push_back(eastl::move(entry));

            glyphs.push_back(glyph);
            ++output.stats.glyphs;
        }
    }

    if (baked.empty())
        return LogAndMakeError("BakeFont: no glyphs with outlines were baked (check ranges)");

    // One pixel of empty border between glyphs: the SDF is already padded, but bilinear
    // filtering must not blend one glyph's field into its neighbour.
    constexpr int kGlyphGap = 1;
    eastl::vector<stbrp_rect> rects(baked.size());
    for (size_t i = 0; i < baked.size(); ++i) {
        rects[i].id = static_cast<int>(i);
        rects[i].w = static_cast<stbrp_coord>(baked[i].width + kGlyphGap * 2);
        rects[i].h = static_cast<stbrp_coord>(baked[i].height + kGlyphGap * 2);
    }

    eastl::vector<stbrp_node> nodes(settings.atlas_width);
    stbrp_context pack_context{};
    stbrp_init_target(&pack_context, static_cast<int>(settings.atlas_width), static_cast<int>(settings.atlas_height),
                      nodes.data(), static_cast<int>(nodes.size()));
    if (!stbrp_pack_rects(&pack_context, rects.data(), static_cast<int>(rects.size())))
        return LogAndMakeError("BakeFont: atlas {}x{} is too small for {} glyphs; enlarge it or trim ranges",
                               settings.atlas_width, settings.atlas_height, baked.size());

    eastl::vector<uint8_t> atlas(static_cast<size_t>(settings.atlas_width) * settings.atlas_height, 0);
    uint64_t used_area = 0;
    for (size_t i = 0; i < baked.size(); ++i) {
        const stbrp_rect& rect = rects[i];
        const BakedGlyph& source = baked[static_cast<size_t>(rect.id)];

        for (int y = 0; y < source.height; ++y) {
            const size_t dst = static_cast<size_t>(rect.y + kGlyphGap + y) * settings.atlas_width +
                               static_cast<size_t>(rect.x + kGlyphGap);
            const size_t src = static_cast<size_t>(y) * static_cast<size_t>(source.width);
            std::memcpy(atlas.data() + dst, source.sdf.data() + src, static_cast<size_t>(source.width));
        }
        used_area += static_cast<uint64_t>(rect.w) * static_cast<uint64_t>(rect.h);
    }
    output.stats.atlas_used_permille = static_cast<uint32_t>(
        used_area * 1000ull /
        (static_cast<uint64_t>(settings.atlas_width) * static_cast<uint64_t>(settings.atlas_height)));

    // Fill atlas bounds now that every glyph knows where it landed.
    for (size_t i = 0; i < baked.size(); ++i) {
        const stbrp_rect& rect = rects[i];
        const BakedGlyph& source = baked[static_cast<size_t>(rect.id)];
        for (FontGlyph& glyph : glyphs) {
            if (glyph.codepoint != source.codepoint)
                continue;
            const float left = static_cast<float>(rect.x + kGlyphGap);
            const float top = static_cast<float>(rect.y + kGlyphGap);
            glyph.plane_left = static_cast<float>(source.xoff);
            glyph.plane_top = static_cast<float>(source.yoff);
            glyph.plane_right = glyph.plane_left + static_cast<float>(source.width);
            glyph.plane_bottom = glyph.plane_top + static_cast<float>(source.height);
            glyph.atlas_left = left / static_cast<float>(settings.atlas_width);
            glyph.atlas_top = top / static_cast<float>(settings.atlas_height);
            glyph.atlas_right = (left + static_cast<float>(source.width)) / static_cast<float>(settings.atlas_width);
            glyph.atlas_bottom = (top + static_cast<float>(source.height)) / static_cast<float>(settings.atlas_height);
            break;
        }
    }

    TextureHeader texture_header{};
    texture_header.format = TextureFormat::VK_FORMAT_R8_UNORM;
    texture_header.type = TextureType::VK_IMAGE_TYPE_2D;
    texture_header.view_type = TextureViewType::VK_IMAGE_VIEW_TYPE_2D;
    texture_header.width = settings.atlas_width;
    texture_header.height = settings.atlas_height;
    texture_header.depth = 1;
    texture_header.array_size = 1;
    texture_header.face_count = 1;
    texture_header.mip_count = 1;
    texture_header.sample_count = TextureSampleCount::VK_SAMPLE_COUNT_1_BIT;
    texture_header.sampler.min_filter = VkFilter::VK_FILTER_LINEAR;
    texture_header.sampler.mag_filter = VkFilter::VK_FILTER_LINEAR;
    texture_header.sampler.mipmap_mode = VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_NEAREST;
    texture_header.sampler.address_mode_u = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    texture_header.sampler.address_mode_v = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    texture_header.sampler.address_mode_w = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    eastl::vector<uint8_t> atlas_blob =
        TextureBinary::Pack(texture_header, eastl::span<const uint8_t>(atlas.data(), atlas.size()));

    FontHeader font_header{};
    font_header.mode = static_cast<uint32_t>(tryengine::resources::FontFieldMode::Sdf);
    font_header.em_size = settings.em_size;
    font_header.px_range = settings.px_range;
    font_header.onedge_value = static_cast<float>(settings.onedge_value);
    font_header.pixel_dist_scale = settings.pixel_dist_scale;
    font_header.line_height = static_cast<float>(ascent - descent + line_gap) * scale;
    font_header.ascender = static_cast<float>(ascent) * scale;
    font_header.descender = static_cast<float>(descent) * scale;

    output.bytes = tryengine::resources::FontBinary::Pack(font_header, glyphs, {}, atlas_blob);
    if (output.bytes.empty())
        return LogAndMakeError("BakeFont: produced an empty FontBinary");
    return output;
}

}  // namespace tryeditor
