// Checks that a .ttf bakes into a valid FontBinary without launching the editor: statistics,
// glyph metrics, atlas coverage and a round-trip through the same Unpack the runtime uses.
//
//   tools/agent/font-probe.sh game/assets/fonts/Main.ttf [--out /tmp/main.font]
//
// Built on demand into tools/agent/.tmp; never part of the game or the editor.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "editor/import/FontBaker.hpp"
#include "engine/resources/binary/FontBinary.hpp"
#include "engine/resources/binary/TextureBinary.hpp"

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

std::vector<uint8_t> ReadFile(const char* path) {
    std::vector<uint8_t> bytes;
    FILE* file = std::fopen(path, "rb");
    if (!file)
        return bytes;
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size > 0) {
        bytes.resize(static_cast<size_t>(size));
        if (std::fread(bytes.data(), 1, bytes.size(), file) != bytes.size())
            bytes.clear();
    }
    std::fclose(file);
    return bytes;
}

bool WriteFile(const char* path, eastl::span<const uint8_t> bytes) {
    FILE* file = std::fopen(path, "wb");
    if (!file)
        return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    std::fclose(file);
    return ok;
}

}  // namespace

int main(int argc, char** argv) {
    const char* path = nullptr;
    const char* out_path = nullptr;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            out_path = argv[++i];
        else if (!path)
            path = argv[i];
    if (!path) {
        std::printf("usage: font-probe <font.ttf> [--out <font.bin>]\n");
        return 2;
    }

    const std::vector<uint8_t> ttf = ReadFile(path);
    if (ttf.empty()) {
        std::printf("FAIL: cannot read '%s'\n", path);
        return 1;
    }

    tryeditor::FontBakeSettings settings{};
    auto baked = tryeditor::BakeFont(eastl::span<const uint8_t>(ttf.data(), ttf.size()), settings);
    if (!baked.has_value()) {
        std::printf("FAIL: bake error: %s\n", baked.error().Message());
        return 1;
    }

    const tryeditor::FontBakeStats& stats = baked->stats;
    std::printf("font: %s\n", path);
    std::printf("  bytes            : %zu\n", static_cast<size_t>(baked->bytes.size()));
    std::printf("  codepoints       : %u\n", stats.codepoints);
    std::printf("  glyphs (outline) : %u\n", stats.glyphs);
    std::printf("  empty (space)    : %u\n", stats.empty_glyphs);
    std::printf("  missing          : %u\n", stats.missing_glyphs);
    std::printf("  atlas            : %ux%u, used %u permille\n", stats.atlas_width, stats.atlas_height,
                stats.atlas_used_permille);
    std::printf("  max advance      : %.2f (em %.1f)\n", stats.max_advance, settings.em_size);

    Check(stats.glyphs > 0, "at least one glyph is baked");
    Check(stats.codepoints == stats.glyphs + stats.empty_glyphs + stats.missing_glyphs,
          "every requested codepoint is accounted for");
    Check(stats.atlas_used_permille > 0 && stats.atlas_used_permille < 1000, "atlas is used but not full");

    auto unpacked =
        tryengine::resources::FontBinary::Unpack(eastl::span<const uint8_t>(baked->bytes.data(), baked->bytes.size()));
    if (!unpacked.has_value()) {
        std::printf("FAIL: FontBinary::Unpack: %s\n", unpacked.error().Message());
        return 1;
    }

    const auto& font = *unpacked;
    Check(font.header.glyph_count == stats.glyphs + stats.empty_glyphs, "unpacked glyph count matches the bake");
    Check(font.header.glyph_count == font.glyphs.size(), "glyph table size matches the header");
    Check(font.header.em_size > 0.0f, "em_size is set");
    Check(font.header.px_range > 0.0f, "px_range is set");
    Check(font.header.line_height > 0.0f, "line_height is positive");
    Check(font.header.ascender > 0.0f && font.header.descender < 0.0f, "ascender/descender have the right signs");
    Check(font.header.onedge_value > 0.0f && font.header.onedge_value < 255.0f, "onedge leaves distance range");
    Check(font.header.pixel_dist_scale != 0.0f, "pixel_dist_scale is set");

    // A font may legitimately lack some of the extended charset: the UI falls back to '?'.
    // Missing ASCII is a real problem, missing non-ASCII is reported but not fatal.
    uint32_t missing_ascii = 0;
    for (uint32_t cp = 0x20; cp <= 0x7E; ++cp) {
        bool found = false;
        for (const auto& glyph : font.glyphs) {
            if (glyph.codepoint == cp) {
                found = true;
                break;
            }
        }
        if (!found)
            ++missing_ascii;
    }

    uint32_t textured_glyphs = 0;
    for (const auto& glyph : font.glyphs) {
        Check(glyph.advance >= 0.0f, "advance is non-negative");
        if (glyph.plane_right <= glyph.plane_left)
            continue;
        ++textured_glyphs;
        Check(glyph.plane_bottom > glyph.plane_top, "glyph plane has height");
        Check(glyph.atlas_left >= 0.0f && glyph.atlas_right <= 1.0f && glyph.atlas_left < glyph.atlas_right,
              "glyph atlas u range is inside the atlas");
        Check(glyph.atlas_top >= 0.0f && glyph.atlas_bottom <= 1.0f && glyph.atlas_top < glyph.atlas_bottom,
              "glyph atlas v range is inside the atlas");
    }
    Check(textured_glyphs == stats.glyphs, "every outline glyph has a textured quad");
    Check(missing_ascii == 0, "the font covers printable ASCII");
    if (stats.missing_glyphs > 0) {
        std::printf("  note             : %u codepoints of the configured ranges are absent (fallback '?')\n",
                    stats.missing_glyphs);
    }

    auto texture = tryengine::resources::TextureBinary::UnpackFull(font.atlas_blob);
    if (!texture.has_value()) {
        std::printf("FAIL: atlas TextureBinary::UnpackFull: %s\n", texture.error().Message());
        return 1;
    }
    Check(texture->header.width == stats.atlas_width && texture->header.height == stats.atlas_height,
          "atlas dimensions match");
    Check(texture->mips.size() == 1, "atlas has a single mip");
    Check(texture->mips[0].size == static_cast<uint64_t>(stats.atlas_width) * static_cast<uint64_t>(stats.atlas_height),
          "atlas pixel payload size matches R8 dimensions");

    if (out_path) {
        if (!WriteFile(out_path, eastl::span<const uint8_t>(baked->bytes.data(), baked->bytes.size()))) {
            std::printf("FAIL: cannot write '%s'\n", out_path);
            ++failures;
        } else {
            std::printf("  written to       : %s\n", out_path);
        }
    }

    if (failures != 0) {
        std::printf("FONT PROBE FAILED (%d checks)\n", failures);
        return 1;
    }
    std::printf("FONT PROBE OK\n");
    return 0;
}
