#pragma once

#include "engine/graphics/RenderCommon.hpp"

namespace tryengine::graphics {

struct Mesh;
struct Material;

struct MeshDrawCall {
    hlslpp::float4x4 model_matrix;
    const Mesh* mesh = nullptr;
    const Material* material = nullptr;
    uint64_t sorting_key = 0;
    uint64_t entity_id;
};

}  // namespace tryengine::graphics