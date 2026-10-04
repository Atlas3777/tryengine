#include "GameRender.hpp"

#include "engine/core/Engine.hpp"
#include "engine/graphics/OpaquePass.hpp"
#include "engine/graphics/PresentPass.hpp"
#include "engine/graphics/VulkanDevice.hpp"

namespace trygame {

GameRender::GameRender(tryengine::graphics::VulkanDevice& device,
                       tryengine::graphics::RenderGraph& render_graph,
                       tryengine::graphics::VulkanSwapchain& swapchain,
                       tryengine::graphics::FrameSync& frame_sync)
    : rg_(render_graph), swapchain_(swapchain), frame_sync_(frame_sync) {

    // В отличие от редактора, здесь регистрируются только игровые пассы
    rg_.AddPass<tryengine::graphics::OpaquePass>();
    rg_.AddPass<tryengine::graphics::PresentPass>();
}

GameRender::~GameRender() {
    rg_.GetDevice().WaitIdle();
}

void GameRender::RecreateSwapchain(int w, int h, const tryengine::graphics::VulkanDevice& device) const {
    device.WaitIdle();

    if (w == 0 || h == 0) {
        return;
    }

    swapchain_.Recreate(static_cast<uint32_t>(w), static_cast<uint32_t>(h), swapchain_.IsVsync());
}

void GameRender::Render(tryengine::core::Engine& engine, tryengine::graphics::VulkanDevice& device) const {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(device.GetWindow(), &w, &h);

    if (w == 0 || h == 0) {
        return;
    }

    const VkExtent2D current_extent = swapchain_.GetExtent();
    if (static_cast<uint32_t>(w) != current_extent.width || static_cast<uint32_t>(h) != current_extent.height) {
        RecreateSwapchain(w, h, device);
        return;
    }

    uint32_t image_index = 0;
    if (!frame_sync_.BeginFrame(device, swapchain_, image_index)) {
        RecreateSwapchain(w, h, device);
        return;
    }

    const uint32_t frame_index = frame_sync_.GetCurrentFrameIndex();
    VkCommandBuffer cmd = frame_sync_.GetCurrentFrame().command_buffer;

    rg_.BeginFrame(frame_index);

    ImportSwapchain(image_index);

    rg_.Collect(engine);
    rg_.Compile();
    rg_.Execute(cmd);

    if (!frame_sync_.EndFrameAndPresent(device, swapchain_, image_index)) {
        RecreateSwapchain(w, h, device);
    }
}

void GameRender::ImportSwapchain(uint32_t image_index) const {
    const VkExtent2D swap_extent = swapchain_.GetExtent();
    const tryengine::graphics::RGTextureDesc swap_desc{
        .width = swap_extent.width,
        .height = swap_extent.height,
        .layers = 1,
        .mips = 1,
        .format = swapchain_.GetImageFormat(),
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
        .debug_name = "SwapchainTexture"
    };

    rg_.ImportExternalTexture(tryengine::graphics::RGTag_v<"Swapchain">,
                              swapchain_.GetImages()[image_index],
                              swapchain_.GetImageViews()[image_index],
                              swap_desc,
                              VK_IMAGE_LAYOUT_UNDEFINED);
}

}  // namespace trygame