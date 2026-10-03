#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <daScript/misc/vectypes.h>

#include "engine/core/Engine.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"
#include "engine/resources/ResourceHandle.hpp"

namespace tryeditor {

struct DebugLine {
    das::float3 start;
    uint8_t r,g,b,a;
    das::float3 end;
    uint32_t _pad;
};

struct Mouse {
    das::float2 pos = {0,0};
    bool pressed = false;
    bool is_down = false;
};

struct EditorFrame {
    eastl::vector<DebugLine> debug_lines;
    Mouse* mouse;
};

struct DebugDrawPass {
    eastl::span<const DebugLine> debug_lines;
    tryengine::graphics::RGResourceHandle lines_buffer;
    tryengine::graphics::RGResourceHandle color_target;
    tryengine::graphics::RGResourceHandle depth_target;
    tryengine::resources::ResourceHandle<tryengine::graphics::Shader> shader;
};

inline eastl::vector<DebugLine> CollectDebug(tryengine::core::Engine& engine) {
    auto debug_res = engine.Get<tryengine::core::ScriptSystem>().SimpleReturnUnsafe<das::Array*>("GetDebugLines");

    if (!debug_res.has_value())
        LogError(LogCategory::Script, "Error on invoke script");

    const auto array = *debug_res;
    DebugLine* points = reinterpret_cast<DebugLine*>(array->data);
    // LogInfo("CollectDebug size = {}", array->size);
    auto vector = eastl::vector<DebugLine>(points, points + array->size);
    engine.Get<tryengine::core::ScriptSystem>().InvokeFunctionFast("ClearDebugLines");
    return vector;
}
}