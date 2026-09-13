#pragma once

#include <EASTL/vector.h>
#include <daScript/daScript.h>
#include <hlsl++/vector_float_type.h>

#include "engine/core/Engine.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryengine::graphics {

struct alignas(16) PointLightGPU {
    hlslpp::float4 position_radius;  // xyz = position, w = radius
    hlslpp::float4 color_intensity;  // rgb = color,    w = intensity
};

struct LightPass {
    RGResourceHandle buffer;
    eastl::span<const PointLightGPU> point_lights_queue;
};

inline eastl::vector<PointLightGPU> CollectLight(core::Engine& engine) {
    auto result = engine.Get<core::ScriptSystem>().SimpleReturnUnsafe<das::Array*>("get_lights");

    if (!result.has_value())
        LogError(result.error().Message());

    const auto array = *result;
    PointLightGPU* points = reinterpret_cast<PointLightGPU*>(array->data);
    // LogInfo("light's size = {}", array->size);

    return eastl::vector<PointLightGPU>(points, points + array->size);
}

}