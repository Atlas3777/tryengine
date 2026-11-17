#pragma once
#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/resources/ResourceHandle.hpp"

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <vk_mem_alloc.h>
#include <volk.h>

#include "editor/PlayModeState.hpp"
#include "engine/graphics/FrameContext.hpp"
#include "engine/graphics/VulkanSwapchain.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryeditor {

class EditorRender {
public:
    static constexpr uint32_t kEditorScope = 1;
    static constexpr uint32_t kGameScope = 2;

    EditorRender(tryengine::core::Engine& engine, tryengine::graphics::VulkanDevice& device,
                 tryengine::graphics::RenderGraph& render_graph, tryengine::graphics::VulkanSwapchain& swapchain,
                 tryengine::graphics::FrameSync& frame_sync);

    ~EditorRender();

    EditorRender(const EditorRender&) = delete;
    EditorRender& operator=(const EditorRender&) = delete;
    EditorRender(EditorRender&&) = delete;
    EditorRender& operator=(EditorRender&&) = delete;

    void Render(tryengine::core::Engine& engine, tryengine::graphics::VulkanDevice& device, PlayModeState& state);

    void RecreateSwapchain(int w, int h, const tryengine::graphics::VulkanDevice& device) const;

    /// VSync the swapchain is running with right now.
    [[nodiscard]] bool IsVSync() const {
        return swapchain_.IsVsync();
    }

    /// Asks for the other VSync mode. The swapchain is recreated at the next frame start: doing
    /// it from the panel would destroy images the command buffer being recorded already refers to.
    void RequestVSync(bool enabled) {
        requested_vsync_ = enabled;
        vsync_pending_ = true;
    }

private:
    void ImportSwapchain(uint32_t image_index) const;
    /// Applies a pending VSync request; returns true when the swapchain was rebuilt.
    bool ApplyPendingVSync(int width, int height);

    bool requested_vsync_ = false;
    bool vsync_pending_ = false;

    VkExtent2D last_viewport_size_{0, 0};
    VkExtent2D last_game_viewport_size_{0, 0};
    tryengine::graphics::RenderGraph& rg_;
    tryengine::graphics::VulkanSwapchain& swapchain_;
    tryengine::graphics::FrameSync& frame_sync_;
};

}  // namespace tryeditor