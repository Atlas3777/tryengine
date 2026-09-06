#pragma once

#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/resources/TextureBinary.hpp"

namespace tryengine::graphics {

struct ShaderAssetParam {
    eastl::string name;
    ShaderParamType type;
    eastl::vector<float> default_values;
};

struct ShaderAssetTexture {
    eastl::string name;
    uint32_t slot;
};

struct ShaderAsset {
    uint64_t vertex_shader_id = 0;
    uint64_t fragment_shader_id = 0;
    eastl::vector<ShaderAssetParam> params;
    eastl::vector<ShaderAssetTexture> textures;
};

struct TextureBindingAssets {
    uint64_t texture_id = 0;
    resources::Sampler sampler;
};

struct MaterialAsset {
    eastl::string name;
    uint64_t shader_asset_id;
    eastl::unordered_map<eastl::string, eastl::vector<float>> scalar_params;

    eastl::unordered_map<eastl::string, TextureBindingAssets> texture_params;
};

}  // namespace tryengine::graphics