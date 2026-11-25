#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/vector.h>
#include <vk_mem_alloc.h>

#include "engine/graphics/rg/IRenderPass.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryengine::graphics {
class VulkanSwapchain;
}

namespace tryeditor {

class ImGuiPass : public tryengine::graphics::IRenderPass {
public:
    const char* Name() const override {
        return "ImGUI";
    };

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

    // --- Интерфейс IRenderPass ---
    void Setup(tryengine::graphics::RenderGraphBuilder& builder) override;
    void Collect(tryengine::graphics::CollectContext& ctx) override;
    void Execute(tryengine::graphics::RGExecuteContext& ctx) override;

    [[nodiscard]] VkDescriptorSet GetEditorViewportTexture();
    [[nodiscard]] VkDescriptorSet GetGameViewportTexture();

    //! Дескриптор для превью текстуры-ассета в панели материала. Кешируется по guid, а не
    //! по view: перезапечённая текстура приходит с новым view, и старый набор уходит в
    //! retired. Вызывать только с main thread, между кадрами ImGui.
    [[nodiscard]] VkDescriptorSet GetAssetTexture(uint64_t guid, VkImageView view, VkSampler sampler);

    void ClearTextureCache();

private:
    [[nodiscard]] VkDescriptorSet GetTexture(tryengine::graphics::ScopeId view, tryengine::graphics::RGTag tag);
    void RetireDescriptorSet(VkDescriptorSet set);
    void PurgeRetiredDescriptorSets();

    tryengine::graphics::RenderGraph& rg_;

    tryengine::graphics::VulkanDevice& device_;
    tryengine::graphics::VulkanSwapchain& swapchain_;

    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    // Текстура вьюпорта переезжает на новый VkImageView при каждой пересборке графа
    // (размер панели поменялся). Храним набор по паре (scope, tag), а не по view:
    // иначе каждый шаг ресайза оставлял бы в пуле навсегда новый дескриптор.
    struct ViewportTexture {
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };

    struct RetiredTextureSet {
        VkDescriptorSet set = VK_NULL_HANDLE;
        uint64_t retired_frame = 0;
    };

    eastl::hash_map<tryengine::graphics::RGTagKey, ViewportTexture> viewport_textures_;

    struct AssetTexture {
        VkImageView view = VK_NULL_HANDLE;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };

    eastl::hash_map<uint64_t, AssetTexture> asset_textures_;
    eastl::vector<RetiredTextureSet> retired_sets_;
    uint64_t frame_number_ = 0;

    SetupData data_{};
};

}  // namespace tryeditor