#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <volk.h>
#include <vk_mem_alloc.h>

#include <EASTL/array.h>
#include <EASTL/vector.h>

#include "CollectDebug.hpp"
#include "PlayModeState.hpp"
#include "engine/graphics/ForwardPipeline.hpp"
#include "engine/graphics/FrameContext.hpp"
#include "engine/graphics/PipelineManager.hpp"
#include "engine/graphics/VulkanDevice.hpp"
#include "engine/graphics/VulkanSwapchain.hpp"
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
    EditorRender(tryengine::graphics::VulkanDevice& device, tryengine::graphics::RenderGraph& render_graph,
                 tryengine::graphics::VulkanSwapchain& swapchain, tryengine::graphics::FrameSync& frame_sync);

    ~EditorRender();

    EditorRender(const EditorRender&) = delete;
    EditorRender& operator=(const EditorRender&) = delete;
    EditorRender(EditorRender&&) = delete;
    EditorRender& operator=(EditorRender&&) = delete;

    void Render(tryengine::core::Engine& engine, tryengine::graphics::VulkanDevice& device, PlayModeState& state);

    void SetDebugShader(const tryengine::resources::ResourceHandle<tryengine::graphics::Shader>& shader) {
        debug_shader_ = shader;
    }


    [[nodiscard]] VkDescriptorSet GetOrCreateImguiTexture(VkImageView view);
    void ClearTextureCache();

private:

    void BuildEditorRenderGraph(const tryengine::graphics::FrameRenderData& frame_render_data,
                                const EditorFrame& editor_frame, RGResourceHandle swapchain_handle, uint32_t width,
                                uint32_t height) const;

    void RecordImguiFrame(tryengine::core::Engine& engine, PlayModeState& state);
    void DrawDockSpace();
    void DestroyPendingDebugStaging(uint32_t frame_index);
    void RecreateSwapchain(int w, int h, tryengine::graphics::VulkanDevice& device);

    VkSampler imgui_sampler_ = VK_NULL_HANDLE;
    eastl::hash_map<VkImageView, VkDescriptorSet> imgui_texture_cache_;

    tryengine::graphics::RenderGraph& rg_;

    tryengine::graphics::VulkanSwapchain& swapchain_;
    tryengine::graphics::FrameSync& frame_sync_;

    tryengine::resources::ResourceHandle<tryengine::graphics::Shader> debug_shader_;

    VkDescriptorPool imgui_descriptor_pool_ = VK_NULL_HANDLE;

    struct StagingAlloc {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
    };
    eastl::array<eastl::vector<StagingAlloc>, tryengine::graphics::MAX_FRAMES_IN_FLIGHT> pending_debug_staging_;
};

}  // namespace tryeditor