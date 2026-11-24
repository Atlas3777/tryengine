#pragma once

#include <EASTL/vector.h>
#include <daScript/daScript.h>

#include "engine/core/Engine.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryengine::graphics {

struct alignas(16) PointLightGPU {
    das::float4 position_radius;  // xyz = position, w = radius
    das::float4 color_intensity;  // rgb = color,    w = intensity
};

struct LightPass {
    RGResourceHandle buffer;
    eastl::span<const PointLightGPU> point_lights_queue;
};

inline eastl::vector<PointLightGPU> CollectLight(core::Engine& engine) {
    auto result = engine.Get<core::ScriptSystem>().SimpleReturnUnsafe<"get_lights", das::Array*>();

    if (!result.has_value())
        LogError(result.error().Message());

    const auto array = *result;
    PointLightGPU* points = reinterpret_cast<PointLightGPU*>(array->data);

    return eastl::vector<PointLightGPU>(points, points + array->size);
}

}  // namespace tryengine::graphics