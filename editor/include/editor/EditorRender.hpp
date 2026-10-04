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
    EditorRender(tryengine::core::Engine& engine, tryengine::graphics::VulkanDevice& device, tryengine::graphics::RenderGraph& render_graph,
                 tryengine::graphics::VulkanSwapchain& swapchain, tryengine::graphics::FrameSync& frame_sync);

    ~EditorRender();

    EditorRender(const EditorRender&) = delete;
    EditorRender& operator=(const EditorRender&) = delete;
    EditorRender(EditorRender&&) = delete;
    EditorRender& operator=(EditorRender&&) = delete;

    void Render(tryengine::core::Engine& engine, tryengine::graphics::VulkanDevice& device, PlayModeState& state) const;

    void RecreateSwapchain(int w, int h, const tryengine::graphics::VulkanDevice& device) const;

private:
    void ImportSwapchain(uint32_t image_index) const;

    tryengine::graphics::RenderGraph& rg_;
    tryengine::graphics::VulkanSwapchain& swapchain_;
    tryengine::graphics::FrameSync& frame_sync_;
};

}  // namespace tryeditor