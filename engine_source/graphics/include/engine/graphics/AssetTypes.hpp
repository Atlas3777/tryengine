#pragma once

#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/resources/TextureBinary.hpp"

namespace tryengine::graphics {

struct TextureBindingAssets {
    uint64_t texture_id = 0;
};

struct MaterialAsset {
    eastl::string name;
    uint64_t shader_asset_id;
    eastl::unordered_map<eastl::string, eastl::vector<float>> scalar_params;

    eastl::unordered_map<eastl::string, TextureBindingAssets> texture_params;
};

}  // namespace tryengine::graphics