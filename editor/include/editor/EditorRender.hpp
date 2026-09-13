#pragma once

#include "CollectDebug.hpp"
#include "PlayModeState.hpp"
#include "engine/graphics/ForwardPipeline.hpp"
#include "engine/graphics/GraphicsContext.hpp"
#include "engine/graphics/PipelineManager.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryeditor {

using tryengine::graphics::RGResourceHandle;
using tryengine::graphics::RGTag;
using tryengine::graphics::RGTag_v;

struct ImGuiPassData {
    RGResourceHandle scene_tex_read;
    RGResourceHandle swapchain_target;
};

class EditorRender {
public:
    explicit EditorRender(tryengine::graphics::GraphicsContext& context, tryengine::graphics::RenderGraph& render_graph);

    ~EditorRender();

    void Render(tryengine::core::Engine& engine, tryengine::graphics::GraphicsContext& context,
                          PlayModeState& state);
    void SetDebugShader(const tryengine::resources::ResourceHandle<tryengine::graphics::Shader>& shader){debug_shader_ = shader;}


private:
    void BuildEditorRenderGraph(const tryengine::graphics::FrameRenderData& frame_render_data, const EditorFrame& editor_frame, RGResourceHandle swapchain_handle, uint32_t width,
                                uint32_t height) const;

    void RecordImguiFrame(tryengine::core::Engine& engine, PlayModeState& state);
    void DrawDockSpace();

    void EnsureDebugPipeline();

    tryengine::graphics::RenderGraph& rg_;
    tryengine::graphics::PipelineManager pm_;
    tryengine::resources::ResourceHandle<tryengine::graphics::Shader> debug_shader_;
    SDL_GPUGraphicsPipeline* debug_pipeline_ = nullptr;
};

}  // namespace tryeditor