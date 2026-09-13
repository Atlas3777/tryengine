#include "GameRender.hpp"

#include "engine/core/Log.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/ForwardPipeline.hpp"

namespace trygame {

GameRender::GameRender(tryengine::graphics::GraphicsContext& context,
                       tryengine::graphics::RenderGraph& render_graph)
    : rg_(render_graph), pm_(context.GetDevice()) {}

void GameRender::Render(tryengine::core::Engine& engine,
                        tryengine::graphics::GraphicsContext& context) {
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(context.GetDevice());

    SDL_GPUTexture* swap_tex = nullptr;
    uint32_t w = 0, h = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, context.GetWindow(), &swap_tex, &w, &h)) {
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    auto camera_data =
        engine.Get<tryengine::core::ScriptSystem>()
            .SimpleReturnUnsafe<tryengine::graphics::CameraData*>("get_camera");

    if (!camera_data.has_value() || !*camera_data) {
        LogCritical("Camera data not found in GameRender");
        SDL_SubmitGPUCommandBuffer(cmd);
        return;
    }

    const auto camera = *camera_data;

    const auto frame_render_data = tryengine::graphics::CollectFrameRenderData(engine, pm_);

    rg_.Reset();
    tryengine::graphics::SetupFrameConstants(rg_, *camera);

    // 2. Импорт Swapchain в RenderGraph
    auto swapchain_format = SDL_GetGPUSwapchainTextureFormat(context.GetDevice(), context.GetWindow());
    tryengine::graphics::RGTextureDesc swap_desc{
        w, h, 1, 0, swapchain_format, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET, "SwapchainTexture"};
    tryengine::graphics::RGResourceHandle swapchain_handle =
        rg_.ImportExternalTexture("Swapchain", swap_tex, swap_desc);

    auto forward_out = tryengine::graphics::BuildForwardPipeline(rg_, frame_render_data, w, h);

    tryengine::graphics::AddBlitPass(rg_, forward_out.color_target, swapchain_handle, w, h, w, h);

    rg_.Compile();
    rg_.Execute(cmd);

    SDL_SubmitGPUCommandBuffer(cmd);
}

}  // namespace trygame