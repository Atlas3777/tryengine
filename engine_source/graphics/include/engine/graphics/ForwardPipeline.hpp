#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <SDL3/SDL_gpu.h>

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

struct BlitPassData {
    RGResourceHandle src;
    RGResourceHandle dst;
};

// 1. Сбор видимой геометрии и источников света кадра
FrameRenderData CollectFrameRenderData(core::Engine& engine, PipelineManager& pm);

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

// 5. Пасс копирования/масштабирования текстуры (например, из SceneColor в Swapchain)
void AddBlitPass(
    RenderGraph& rg,
    RGResourceHandle src_handle,
    RGResourceHandle dst_handle,
    uint32_t src_w, uint32_t src_h,
    uint32_t dst_w, uint32_t dst_h
);

}  // namespace tryengine::graphics