#pragma once

#include "MeshDrawCall.hpp"
#include "engine/core/Engine.hpp"
#include "rg/RenderGraph.hpp"

namespace tryengine::graphics {

class RGExecuteContext;
class RenderQueueRegistry;
class PipelineManager;
struct Material;
struct Mesh;

struct OpaquePass {
    RGResourceHandle color_target;
    RGResourceHandle depth_target;
    RGResourceHandle light_buffer;
    eastl::span<const MeshDrawCall> opaque_pass_queue;
};


class OpaqueGeometryPass {
public:
    static eastl::vector<MeshDrawCall> CollectDrawable(core::Engine& engine);
    static void ExecuteDrawCommands(const RGExecuteContext& ctx, eastl::span<const MeshDrawCall> queue);
};

}  // namespace tryengine::graphics