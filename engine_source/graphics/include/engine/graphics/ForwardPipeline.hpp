#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <SDL3/SDL_gpu.h>

#include "OpaqueGeometryPass.hpp"
#include "engine/core/Engine.hpp"
#include "engine/graphics/MeshDrawCall.hpp"
#include "engine/graphics/PipelineManager.hpp"
#include "engine/graphics/loaders/LightPass.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryengine::graphics {

struct FrameRenderData {
    eastl::vector<MeshDrawCall> opaque_queue;
    eastl::vector<PointLightGPU> point_lights;
};

struct ForwardPipelineOutput {
    RGResourceHandle color_target;
    RGResourceHandle depth_target;
    RGResourceHandle light_buffer;
};

struct PresentPassData { RGResourceHandle target; };

inline void AddPresentPass(RenderGraph& rg, RGResourceHandle swapchain) {
    rg.AddPass<PresentPassData>("Present",
        [swapchain](RenderGraphBuilder& b, PresentPassData& d) {
            d.target = b.Read(swapchain, RGUsageHint::Present);
            b.MarkSideEffect();
        },
        [](RGExecuteContext&, const PresentPassData&) {});  // тело пустое, вся работа в барьере
}

inline FrameRenderData CollectFrameRenderData(core::Engine& engine) {
    FrameRenderData data;
    data.point_lights = eastl::move(CollectLight(engine));
    data.opaque_queue = eastl::move(OpaqueGeometryPass::CollectDrawable(engine));
    return data;
}

// 2. Установка глобальных констант кадра (Camera, LightUBO)
void SetupFrameConstants(RenderGraph& rg, const CameraData& camera);

// 3. Отдельный пасс загрузки источников света в GPU Storage Buffer
RGResourceHandle AddLightPass(RenderGraph& rg, eastl::span<const PointLightGPU> point_lights);

// 4. Построение изолированного Forward-пайплайна (LightPass + OpaquePass)
// Гарантированно рендерит в offscreen SceneColor/SceneDepth и возвращает их handles.
ForwardPipelineOutput BuildForwardPipeline(
    RenderGraph& rg,
    const FrameRenderData& frame_data,
    uint32_t width,
    uint32_t height
);

}  // namespace tryengine::graphics