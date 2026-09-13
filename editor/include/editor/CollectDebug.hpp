#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <daScript/misc/vectypes.h>

#include "engine/core/Engine.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryeditor {

struct DebugLine {
    das::float4 start;
    das::float4 end;
    uint8_t r,g,b,a;
};

struct EditorFrame {
    eastl::vector<DebugLine> debug_lines;
};

struct DebugDrawPass {
    eastl::span<const DebugLine> debug_lines;
    tryengine::graphics::RGResourceHandle lines_buffer;
    tryengine::graphics::RGResourceHandle color_target;
    tryengine::graphics::RGResourceHandle depth_target;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
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