#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>

#include "../../../../engine_source/resources/include/engine/resources/binary/TextureBinary.hpp"
#include "engine/core/Result.hpp"
#include "engine/resources/TextureTypes.hpp"
#include "tiny_gltf_v3.h"

namespace tryeditor {

inline tryengine::resources::VkFilter MapGltfMagFilter(const int gltf_filter) {
    if (gltf_filter == TG3_TEXTURE_FILTER_NEAREST)
        return tryengine::resources::VkFilter::VK_FILTER_NEAREST;
    return tryengine::resources::VkFilter::VK_FILTER_LINEAR;
}

inline tryengine::resources::VkFilter MapGltfMinFilter(const int gltf_filter) {
    switch (gltf_filter) {
        case TG3_TEXTURE_FILTER_NEAREST:
        case TG3_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST:
        case TG3_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
            return tryengine::resources::VkFilter::VK_FILTER_NEAREST;
        default:
            return tryengine::resources::VkFilter::VK_FILTER_LINEAR;  // 9729, 9985, 9987
    }
}

inline tryengine::resources::VkSamplerMipmapMode MapGltfMipmapMode(const int gltf_filter) {
    switch (gltf_filter) {
        case TG3_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR:
        case TG3_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
            return tryengine::resources::VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_LINEAR;
        default:
            return tryengine::resources::VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }
}

inline tryengine::resources::VkSamplerAddressMode MapGltfWrap(const int gltf_wrap) {
    switch (gltf_wrap) {
        case TG3_TEXTURE_WRAP_CLAMP_TO_EDGE:
            return tryengine::resources::VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case TG3_TEXTURE_WRAP_MIRRORED_REPEAT:
            return tryengine::resources::VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        default:
            return tryengine::resources::VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

struct TextureProcessSettings {
    // Compressed (BC1/BC3/BC7 and friends) or uncompressed 8-bit (R8, R8G8, RGBA8, SRGB).
    tryengine::resources::TextureFormat format = tryengine::resources::TextureFormat::VK_FORMAT_A8B8G8R8_UNORM_PACK32;
    tryengine::resources::SamplerDesc sampler;
    //! Build the whole mip chain. Off keeps one level: UI sprites and pixel art usually want
    //! that, because a mip-blurred panel reads as soft.
    bool generate_mips = true;
    //! Clamp the longest side before mips are generated; 0 keeps the source size.
    uint32_t max_size = 0;
};

struct ProcessedTextureResult {
    uint32_t width = 0;
    uint32_t height = 0;
};

class TextureProcessor {
public:
    static tryengine::Result<eastl::vector<uint8_t>> ProcessFromEncodedMemory(eastl::span<const uint8_t> encoded_bytes,
                                                                              const TextureProcessSettings& settings);

    static tryengine::Result<eastl::vector<uint8_t>> ProcessFromRawPixels(const uint8_t* rgba_data, uint32_t width,
                                                                          uint32_t height,
                                                                          const TextureProcessSettings& settings);
};

}  // namespace tryeditor