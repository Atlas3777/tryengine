#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>

#include "engine/core/Result.hpp"
#include "engine/resources/TextureTypes.hpp"
#include "tiny_gltf_v3.h"

namespace tryeditor {

inline tryengine::resources::TextureFilter MapGltfFilter(const int gltf_filter) {
    switch (gltf_filter) {
        case TG3_TEXTURE_FILTER_NEAREST:
        case TG3_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST:
        case TG3_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR:
            return tryengine::resources::TextureFilter::FILTER_NEAREST;
        default:
            return tryengine::resources::TextureFilter::FILTER_LINEAR;  // 9729, 9985, 9987
    }
}

inline tryengine::resources::SamplerAddressMode MapGltfWrap(const int gltf_wrap) {
    switch (gltf_wrap) {
        case TG3_TEXTURE_WRAP_CLAMP_TO_EDGE:
            return tryengine::resources::SamplerAddressMode::SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        case TG3_TEXTURE_WRAP_MIRRORED_REPEAT:
            return tryengine::resources::SamplerAddressMode::SAMPLERADDRESSMODE_MIRRORED_REPEAT;
        default:
            return tryengine::resources::SamplerAddressMode::SAMPLERADDRESSMODE_REPEAT;  // 10497
    }
}

struct TextureProcessSettings {
    tryengine::resources::TextureFormat format = tryengine::resources::TextureFormat::EXTUREFORMAT_R8G8B8A8_UINT;
};

struct ProcessedTextureResult {
    uint32_t width = 0;
    uint32_t height = 0;
};

class TextureProcessor {
public:
    static tryengine::Result<eastl::vector<uint8_t>> ProcessFromEncodedMemory(
        eastl::span<const uint8_t> encoded_bytes, const TextureProcessSettings& settings);

    static tryengine::Result<eastl::vector<uint8_t>> ProcessFromRawPixels(const uint8_t* rgba_data, uint32_t width,
                                                                          uint32_t height,
                                                                          const TextureProcessSettings& settings);
};

}  // namespace tryeditor