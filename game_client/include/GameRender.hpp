#pragma once

#include "engine/graphics/FrameContext.hpp"
#include "engine/graphics/VulkanSwapchain.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryengine::core {
class Engine;
}

namespace tryengine::graphics {
class VulkanDevice;
}

namespace trygame {

class GameRender {
public:
    GameRender(tryengine::graphics::VulkanDevice& device,
               tryengine::graphics::RenderGraph& render_graph,
               tryengine::graphics::VulkanSwapchain& swapchain,
               tryengine::graphics::FrameSync& frame_sync);
    ~GameRender();

    void Render(tryengine::core::Engine& engine, tryengine::graphics::VulkanDevice& device);

private:
    void RecreateSwapchain(int w, int h, const tryengine::graphics::VulkanDevice& device) const;
    void ImportSwapchain(uint32_t image_index) const;

    VkExtent2D last_viewport_size_{0, 0};
    tryengine::graphics::RenderGraph& rg_;
    tryengine::graphics::VulkanSwapchain& swapchain_;
    tryengine::graphics::FrameSync& frame_sync_;
};

}  // namespace trygame