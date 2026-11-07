#pragma once

#include <vk_mem_alloc.h>
#include <EASTL/hash_map.h>

#include "engine/graphics/rg/IRenderPass.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryengine::graphics {
class VulkanSwapchain;
}
namespace tryeditor {

class ImGuiPass : public tryengine::graphics::IRenderPass {
public:
    const char* Name() const override {return "ImGUI";};
    struct SetupData {
        tryengine::graphics::RGResourceHandle editor_scene_color;
        tryengine::graphics::RGResourceHandle game_scene_color;
        tryengine::graphics::RGResourceHandle swapchain_target;
    };

    ImGuiPass(tryengine::graphics::RenderGraph& rg, tryengine::graphics::VulkanDevice& device,
              tryengine::graphics::VulkanSwapchain& swapchain);
    ~ImGuiPass() override;

    // Запрет копирования/перемещения
    ImGuiPass(const ImGuiPass&) = delete;
    ImGuiPass& operator=(const ImGuiPass&) = delete;

    VkDescriptorSet GetOrCreateTexture(VkImageView view);

    // --- Интерфейс IRenderPass ---
    void Setup(tryengine::graphics::RenderGraphBuilder& builder) override;
    void Collect(tryengine::graphics::CollectContext& ctx) override;
    void Execute(tryengine::graphics::RGExecuteContext& ctx) override;

    [[nodiscard]] VkDescriptorSet GetEditorViewportTexture();
    [[nodiscard]] VkDescriptorSet GetGameViewportTexture();

    void ClearTextureCache();

private:
    [[nodiscard]] VkDescriptorSet GetTexture(tryengine::graphics::ScopeId view, tryengine::graphics::RGTag tag);

    tryengine::graphics::RenderGraph& rg_;

    tryengine::graphics::VulkanDevice& device_;
    tryengine::graphics::VulkanSwapchain& swapchain_;

    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    eastl::hash_map<VkImageView, VkDescriptorSet> texture_cache_;

    SetupData data_{};
};

} // namespace tryeditor