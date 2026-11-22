#include "editor/import/TextureBaker.hpp"

#include <cstring>

#include "editor/import/TextureProcessor.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/resources/binary/TextureBinary.hpp"
#include "stb_image.h"

namespace tryeditor {

using tryengine::core::LogAndMakeError;
using tryengine::resources::MipView;
using tryengine::resources::SamplerDesc;
using tryengine::resources::TextureBinary;
using tryengine::resources::TextureFormat;
using tryengine::resources::VkFilter;
using tryengine::resources::VkSamplerAddressMode;
using tryengine::resources::VkSamplerMipmapMode;

namespace {

bool FormatFromName(eastl::string_view name, TextureFormat& out) {
    if (name == "rgba8")
        out = TextureFormat::VK_FORMAT_R8G8B8A8_UNORM;
    else if (name == "rgba8_srgb")
        out = TextureFormat::VK_FORMAT_R8G8B8A8_SRGB;
    else if (name == "r8")
        out = TextureFormat::VK_FORMAT_R8_UNORM;
    else if (name == "rg8")
        out = TextureFormat::VK_FORMAT_R8G8_UNORM;
    else if (name == "bc1")
        out = TextureFormat::VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    else if (name == "bc1_srgb")
        out = TextureFormat::VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    else if (name == "bc3")
        out = TextureFormat::VK_FORMAT_BC3_UNORM_BLOCK;
    else if (name == "bc3_srgb")
        out = TextureFormat::VK_FORMAT_BC3_SRGB_BLOCK;
    else if (name == "bc7")
        out = TextureFormat::VK_FORMAT_BC7_UNORM_BLOCK;
    else if (name == "bc7_srgb")
        out = TextureFormat::VK_FORMAT_BC7_SRGB_BLOCK;
    else
        return false;
    return true;
}

bool FilterFromName(eastl::string_view name, VkFilter& out) {
    if (name == "linear")
        out = VkFilter::VK_FILTER_LINEAR;
    else if (name == "nearest")
        out = VkFilter::VK_FILTER_NEAREST;
    else
        return false;
    return true;
}

bool WrapFromName(eastl::string_view name, VkSamplerAddressMode& out) {
    if (name == "repeat")
        out = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT;
    else if (name == "clamp")
        out = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    else if (name == "mirror")
        out = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    else
        return false;
    return true;
}

}  // namespace

tryengine::Result<TextureBakeOutput> BakeTexture(eastl::span<const uint8_t> encoded_bytes,
                                                 const TextureImportSettings& settings) {
    if (encoded_bytes.empty())
        return LogAndMakeError("Texture source is empty");

    TextureFormat format = TextureFormat::VK_FORMAT_UNDEFINED;
    if (!FormatFromName(settings.format, format))
        return LogAndMakeError(
            "Texture format '{}' is unknown; use rgba8, rgba8_srgb, r8, rg8, bc1, bc1_srgb, "
            "bc3, bc3_srgb, bc7 or bc7_srgb",
            settings.format.c_str());

    VkFilter filter = VkFilter::VK_FILTER_LINEAR;
    if (!FilterFromName(settings.filter, filter))
        return LogAndMakeError("Texture filter '{}' is unknown; use linear or nearest", settings.filter.c_str());

    VkSamplerAddressMode wrap = VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT;
    if (!WrapFromName(settings.wrap, wrap))
        return LogAndMakeError("Texture wrap '{}' is unknown; use repeat, clamp or mirror", settings.wrap.c_str());

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load_from_memory(encoded_bytes.data(), static_cast<int>(encoded_bytes.size()), &width,
                                            &height, &channels, 4);
    if (pixels == nullptr) {
        const char* reason = stbi_failure_reason();
        return LogAndMakeError("Texture decode failed: {}", reason != nullptr ? reason : "unknown");
    }

    TextureProcessSettings process;
    process.format = format;
    process.generate_mips = settings.generate_mips;
    process.max_size = settings.max_size;
    process.sampler = SamplerDesc{};
    process.sampler.min_filter = filter;
    process.sampler.mag_filter = filter;
    // A single-level texture cannot sample mips: keep the sampler from asking for them.
    process.sampler.mipmap_mode = settings.generate_mips && filter == VkFilter::VK_FILTER_LINEAR
                                      ? VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_LINEAR
                                      : VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_NEAREST;
    process.sampler.address_mode_u = wrap;
    process.sampler.address_mode_v = wrap;
    process.sampler.address_mode_w = wrap;
    process.sampler.compare_op = tryengine::resources::VkCompareOp::VK_COMPARE_OP_NEVER;
    process.sampler.enable_anisotropy = false;
    process.sampler.enable_compare = false;
    process.sampler.max_anisotropy = 1.0f;
    process.sampler.max_lod = 0.0f;

    auto baked = TextureProcessor::ProcessFromRawPixels(pixels, static_cast<uint32_t>(width),
                                                        static_cast<uint32_t>(height), process);
    stbi_image_free(pixels);

    if (!baked.has_value())
        return eastl::move(baked.error());

    // Report the artifact as it will be read at runtime, not as it was requested: max_size and
    // the mip policy are applied inside the processor.
    auto unpacked = TextureBinary::UnpackFull(eastl::span<const uint8_t>(baked->data(), baked->size()));
    if (!unpacked.has_value())
        return LogAndMakeError("Baked texture is not readable: {}", unpacked.error().Message());

    TextureBakeOutput output;
    output.stats.width = unpacked->header.width;
    output.stats.height = unpacked->header.height;
    output.stats.mip_count = unpacked->header.mip_count;
    output.stats.format = static_cast<uint32_t>(unpacked->header.format);
    output.stats.bytes = baked->size();
    output.bytes = eastl::move(*baked);
    return output;
}

}  // namespace tryeditor
