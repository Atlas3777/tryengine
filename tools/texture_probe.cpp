// Checks that an encoded image bakes into a valid TextureBinary artifact without launching the
// editor: format coverage (uncompressed and BC), mip policy, size clamp, sampler mapping and a
// round trip through the same Unpack the runtime uses.
//
//   tools/agent/texture-probe.sh                       # synthetic 64x64 PNG, all cases
//   tools/agent/texture-probe.sh game/assets/ui/logo.png
//
// Built on demand into tools/agent/.tmp; never part of the game or the editor.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// The probe is the only translation unit here that provides the stb implementations: the baker
// and the processor include the headers for declarations only, exactly like the editor build.
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include <glaze/json.hpp>

#include "editor/import/TextureBaker.hpp"
#include "editor/meta/AssetMetaHeader.hpp"
#include "engine/resources/EastlGlazeSerializers.hpp"
#include "engine/resources/binary/TextureBinary.hpp"
#include "stb_image.h"
#include "stb_image_resize2.h"
#include "stb_image_write.h"

namespace {

using tryengine::resources::MipInfo;
using tryengine::resources::TextureBinary;
using tryengine::resources::TextureFormat;
using tryengine::resources::TextureHeader;
using tryengine::resources::VkFilter;
using tryengine::resources::VkSamplerAddressMode;
using tryengine::resources::VkSamplerMipmapMode;

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

// A recognizable image: red/green gradient plus a transparent border and a checker patch, so a
// wrong channel order or a lost alpha shows up in the summary numbers.
std::vector<uint8_t> MakeSyntheticRgba(int width, int height) {
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t* px = &rgba[(static_cast<size_t>(y) * width + x) * 4];
            px[0] = static_cast<uint8_t>(x * 255 / std::max(1, width - 1));
            px[1] = static_cast<uint8_t>(y * 255 / std::max(1, height - 1));
            px[2] = static_cast<uint8_t>(((x / 8) + (y / 8)) % 2 == 0 ? 40 : 200);
            const bool border = x < 2 || y < 2 || x >= width - 2 || y >= height - 2;
            px[3] = static_cast<uint8_t>(border ? 0 : 255);
        }
    }
    return rgba;
}

uint32_t MipCountFor(uint32_t width, uint32_t height) {
    uint32_t count = 1;
    while (width > 1 || height > 1) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        ++count;
    }
    return count;
}

// Independent expectation of one mip's payload size, derived from the format only.
uint64_t ExpectedMipSize(TextureFormat format, uint32_t width, uint32_t height) {
    uint32_t block = 1;
    uint32_t bytes_per_block = 4;
    switch (format) {
        case TextureFormat::VK_FORMAT_R8_UNORM:
            bytes_per_block = 1;
            break;
        case TextureFormat::VK_FORMAT_R8G8_UNORM:
            bytes_per_block = 2;
            break;
        case TextureFormat::VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case TextureFormat::VK_FORMAT_BC4_UNORM_BLOCK:
            block = 4;
            bytes_per_block = 8;
            break;
        case TextureFormat::VK_FORMAT_BC3_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC3_SRGB_BLOCK:
        case TextureFormat::VK_FORMAT_BC7_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC7_SRGB_BLOCK:
            block = 4;
            bytes_per_block = 16;
            break;
        case TextureFormat::VK_FORMAT_R8G8B8A8_UNORM:
        case TextureFormat::VK_FORMAT_R8G8B8A8_SRGB:
        default:
            bytes_per_block = 4;
            break;
    }
    const uint32_t bx = (width + block - 1) / block;
    const uint32_t by = (height + block - 1) / block;
    return static_cast<uint64_t>(bx) * by * bytes_per_block;
}

struct Case {
    const char* name;
    tryeditor::TextureImportSettings settings;
    uint32_t expected_width;
    uint32_t expected_height;
    TextureFormat expected_format;
};

void RunCase(const Case& test, eastl::span<const uint8_t> encoded) {
    auto baked = tryeditor::BakeTexture(encoded, test.settings);
    if (!baked.has_value()) {
        std::printf("FAIL: %s: %s\n", test.name, baked.error().Message());
        ++failures;
        return;
    }

    auto unpacked = TextureBinary::UnpackFull(eastl::span<const uint8_t>(baked->bytes.data(), baked->bytes.size()));
    if (!unpacked.has_value()) {
        std::printf("FAIL: %s: UnpackFull: %s\n", test.name, unpacked.error().Message());
        ++failures;
        return;
    }

    const TextureHeader& header = unpacked->header;
    std::printf("  %-16s %4ux%-4u mips %u  format %3u  %7zu bytes\n", test.name, header.width, header.height,
                header.mip_count, static_cast<uint32_t>(header.format), baked->bytes.size());

    Check(header.width == test.expected_width && header.height == test.expected_height, "dimensions match settings");
    Check(header.format == test.expected_format, "format matches settings");
    Check(header.mip_count == (test.settings.generate_mips ? MipCountFor(header.width, header.height) : 1),
          "mip count follows the settings");
    Check(unpacked->mips.size() == header.mip_count, "mip table has one entry per level");

    uint64_t sum = 0;
    uint32_t w = header.width;
    uint32_t h = header.height;
    for (const auto& mip : unpacked->mips) {
        sum += mip.size;
        Check(mip.size == ExpectedMipSize(header.format, w, h), "mip payload size matches its format");
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    Check(header.mip_table_offset == sizeof(TextureHeader), "mip table follows the header");
    Check(header.mip_table_size == header.mip_count * sizeof(MipInfo), "mip table size matches the level count");
    Check(header.mip_table_offset + header.mip_table_size + sum == baked->bytes.size(),
          "artifact size is header + mip table + payload");
    Check(baked->stats.width == header.width && baked->stats.height == header.height &&
              baked->stats.mip_count == header.mip_count,
          "reported stats match the artifact");
}

}  // namespace

int main(int argc, char** argv) {
    // Input: a PNG on the command line, or a synthetic image encoded to PNG in memory so the
    // probe exercises the same decode path as a real import.
    std::vector<uint8_t> encoded;
    const char* meta_path = nullptr;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--meta") == 0 && i + 1 < argc)
            meta_path = argv[++i];

    if (argc > 1 && argv[1][0] != '-') {
        encoded = ReadFile(argv[1]);
        if (encoded.empty()) {
            std::printf("FAIL: cannot read '%s'\n", argv[1]);
            return 1;
        }
        std::printf("image: %s (%zu bytes)\n", argv[1], encoded.size());
    } else {
        const int width = 64;
        const int height = 64;
        const auto rgba = MakeSyntheticRgba(width, height);
        int png_size = 0;
        unsigned char* png = stbi_write_png_to_mem(rgba.data(), width * 4, width, height, 4, &png_size);
        if (png == nullptr || png_size <= 0) {
            std::printf("FAIL: stbi_write_png_to_mem\n");
            return 1;
        }
        encoded.assign(png, png + png_size);
        STBIW_FREE(png);
        std::printf("image: synthetic %dx%d PNG (%zu bytes)\n", width, height, encoded.size());
    }

    tryeditor::TextureImportSettings base;
    tryeditor::TextureImportSettings no_mips = base;
    no_mips.generate_mips = false;
    tryeditor::TextureImportSettings clamped = base;
    clamped.max_size = 32;
    tryeditor::TextureImportSettings bc7 = base;
    bc7.format = "bc7_srgb";
    tryeditor::TextureImportSettings r8 = base;
    r8.format = "r8";
    r8.generate_mips = false;
    tryeditor::TextureImportSettings ui_sprite = base;
    ui_sprite.format = "rgba8_srgb";
    ui_sprite.generate_mips = false;
    ui_sprite.filter = "nearest";
    ui_sprite.wrap = "clamp";

    const Case cases[] = {
        {"rgba8_srgb+mips", base, 64, 64, TextureFormat::VK_FORMAT_R8G8B8A8_SRGB},
        {"no mips", no_mips, 64, 64, TextureFormat::VK_FORMAT_R8G8B8A8_SRGB},
        {"max_size 32", clamped, 32, 32, TextureFormat::VK_FORMAT_R8G8B8A8_SRGB},
        {"bc7_srgb", bc7, 64, 64, TextureFormat::VK_FORMAT_BC7_SRGB_BLOCK},
        {"r8 no mips", r8, 64, 64, TextureFormat::VK_FORMAT_R8_UNORM},
        {"ui sprite", ui_sprite, 64, 64, TextureFormat::VK_FORMAT_R8G8B8A8_SRGB},
    };

    for (const auto& test : cases)
        RunCase(test, eastl::span<const uint8_t>(encoded.data(), encoded.size()));

    // The sampler is part of the artifact: an importer that forgets it silently samples wrong.
    auto sprite = tryeditor::BakeTexture(eastl::span<const uint8_t>(encoded.data(), encoded.size()), ui_sprite);
    if (sprite.has_value()) {
        auto unpacked =
            TextureBinary::UnpackFull(eastl::span<const uint8_t>(sprite->bytes.data(), sprite->bytes.size()));
        Check(unpacked.has_value(), "ui sprite unpacks");
        if (unpacked.has_value()) {
            const auto& sampler = unpacked->header.sampler;
            Check(sampler.min_filter == VkFilter::VK_FILTER_NEAREST, "sampler min filter follows the settings");
            Check(sampler.mag_filter == VkFilter::VK_FILTER_NEAREST, "sampler mag filter follows the settings");
            Check(sampler.mipmap_mode == VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_NEAREST,
                  "a texture without mips samples without mip filtering");
            Check(sampler.address_mode_u == VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                  "sampler wrap follows the settings");
        }
    } else {
        Check(false, "ui sprite bakes");
    }

    // A real .meta from the project: the importer must read exactly these field names, because a
    // parse failure would fall back to defaults and re-bake the asset with a different look.
    if (meta_path != nullptr) {
        const auto meta_file = ReadFile(meta_path);
        tryeditor::AssetMeta<tryeditor::TextureImportSettings> meta;
        const auto read_error = glz::read_json(meta, eastl::span<const uint8_t>(meta_file.data(), meta_file.size()));
        Check(read_error == glz::error_code::none, "the .meta parses as texture import settings");
        if (read_error == glz::error_code::none) {
            std::printf("meta: %s\n  importer %s  guid %llu\n  format %s  mips %s  max_size %u  filter %s  wrap %s\n",
                        meta_path, meta.header.importer_type.c_str(), static_cast<unsigned long long>(meta.header.guid),
                        meta.settings.format.c_str(), meta.settings.generate_mips ? "on" : "off",
                        meta.settings.max_size, meta.settings.filter.c_str(), meta.settings.wrap.c_str());
            Check(meta.header.importer_type == "TextureImporter", "the .meta names the texture importer");
            Check(!meta.settings.format.empty(), "the .meta carries a format");
        }
    }

    // Unknown names are an import error, not a silently different texture.
    auto bad_format = tryeditor::TextureImportSettings{};
    bad_format.format = "dxt9";
    Check(!tryeditor::BakeTexture(eastl::span<const uint8_t>(encoded.data(), encoded.size()), bad_format).has_value(),
          "an unknown format is rejected");
    auto bad_wrap = tryeditor::TextureImportSettings{};
    bad_wrap.wrap = "tile";
    Check(!tryeditor::BakeTexture(eastl::span<const uint8_t>(encoded.data(), encoded.size()), bad_wrap).has_value(),
          "an unknown wrap is rejected");
    auto empty = tryeditor::BakeTexture(eastl::span<const uint8_t>(), base);
    Check(!empty.has_value(), "an empty source is rejected");

    // The importer reads the settings back from the .meta through glaze, so the field names are
    // a contract: a rename here silently resets every texture to the default settings.
    tryeditor::AssetMeta<tryeditor::TextureImportSettings> meta;
    meta.header.guid = 0xC83E1B7A4D2F9A11ull;
    meta.header.importer_type = "TextureImporter";
    meta.settings = ui_sprite;
    eastl::vector<uint8_t> meta_bytes;
    const auto write_error = glz::write_json(meta, meta_bytes);
    Check(write_error == glz::error_code::none, "texture meta serializes");
    if (write_error == glz::error_code::none) {
        tryeditor::AssetMeta<tryeditor::TextureImportSettings> parsed;
        const auto read_error =
            glz::read_json(parsed, eastl::span<const uint8_t>(meta_bytes.data(), meta_bytes.size()));
        Check(read_error == glz::error_code::none, "texture meta reads back");
        if (read_error == glz::error_code::none) {
            Check(parsed.header.guid == meta.header.guid, "meta keeps the guid");
            Check(parsed.settings.format == "rgba8_srgb", "meta keeps the format");
            Check(!parsed.settings.generate_mips, "meta keeps the mip policy");
            Check(parsed.settings.filter == "nearest" && parsed.settings.wrap == "clamp", "meta keeps the sampler");
        }
    }

    if (failures != 0) {
        std::printf("TEXTURE PROBE FAILED (%d checks)\n", failures);
        return 1;
    }
    std::printf("TEXTURE PROBE OK\n");
    return 0;
}
