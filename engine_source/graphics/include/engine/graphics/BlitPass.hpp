#pragma once

#include "engine/graphics/rg/IRenderPass.hpp"
#include "engine/graphics/rg/RenderGraph.hpp"

namespace tryengine::graphics {

class BlitPass : public IRenderPass {
public:
    BlitPass() {}

    [[nodiscard]] const char* Name() const override { return "BlitPass"; }

    void Setup(RenderGraphBuilder& builder) override {
        data_.scene_color_read = builder.Read(tryengine::graphics::RGTag_v<"ForwardColor">, RGUsageHint::TransferSrc);
        data_.swapchain_target = builder.Write(tryengine::graphics::RGTag_v<"Swapchain">, RGUsageHint::TransferDst);

        builder.MarkSideEffect();
    }

    void Execute(RGExecuteContext& ctx) override {
        VkImage src_image = ctx.GetTexture(data_.scene_color_read);
        VkImage dst_image = ctx.GetTexture(data_.swapchain_target);

        auto& scene = ctx.GetTextureDesc(data_.scene_color_read);
        auto& swap = ctx.GetTextureDesc(data_.swapchain_target);


        if (!src_image || !dst_image) {
            TRY_ASSERT(false, "!src_image || !dst_image");
            return;
        }

        VkImageBlit2 blit_region{};
        blit_region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
        blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit_region.srcSubresource.layerCount = 1;
        blit_region.srcOffsets[0] = {0, 0, 0};
        blit_region.srcOffsets[1] = {static_cast<int32_t>(scene.width), static_cast<int32_t>(scene.height), 1};

        blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit_region.dstSubresource.layerCount = 1;
        blit_region.dstOffsets[0] = {0, 0, 0};
        blit_region.dstOffsets[1] = {static_cast<int32_t>(swap.width), static_cast<int32_t>(swap.height), 1};

        VkBlitImageInfo2 blit_info{};
        blit_info.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
        blit_info.srcImage = src_image;
        blit_info.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        blit_info.dstImage = dst_image;
        blit_info.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        blit_info.regionCount = 1;
        blit_info.pRegions = &blit_region;
        blit_info.filter = VK_FILTER_NEAREST;

        vkCmdBlitImage2(ctx.cmd_buffer, &blit_info);
    }

private:
    struct Data {
        RGResourceHandle scene_color_read;
        RGResourceHandle swapchain_target;
    } data_;
};
}  // namespace tryengine::graphics