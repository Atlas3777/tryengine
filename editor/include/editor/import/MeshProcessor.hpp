#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>

#include "engine/core/Result.hpp"
#include "engine/resources/Vertex.hpp"

namespace tryeditor {

struct RawAttributeStream {
    const uint8_t* data = nullptr;
    uint32_t stride = 0;
    uint32_t count = 0;
};

struct RawIndexStream {
    const uint8_t* data = nullptr;
    uint32_t stride = 0;
    uint32_t count = 0;
    tryengine::resources::IndexFormat format = tryengine::resources::IndexFormat::None;
};

struct RawPrimitiveInput {
    RawAttributeStream positions;
    RawAttributeStream normals;
    RawAttributeStream uvs;
    RawAttributeStream colors;
    RawAttributeStream joints;   // JOINTS_0
    RawAttributeStream weights;  // WEIGHTS_0

    uint32_t color_components = 4;        // 3 (RGB) или 4 (RGBA)
    uint32_t joint_component_type = 5121; // 5121 (u8) или 5123 (u16)
    uint32_t weight_component_type = 5126; // 5126 (float), 5121 (u8_unorm), 5123 (u16_unorm)

    RawIndexStream indices;
};

struct MeshProcessSettings {
    bool generate_aabb = true;
    bool generate_radius = true;
    bool generate_flat_normals_if_missing = true;
    bool auto_select_format = true; // Если true, выбирает подходящий формат на основе атрибутов
};

class MeshProcessor {
public:
    static tryengine::Result<eastl::vector<uint8_t>> ProcessPrimitive(
        const RawPrimitiveInput& input,
        const MeshProcessSettings& settings = {});
};

} // namespace tryeditor