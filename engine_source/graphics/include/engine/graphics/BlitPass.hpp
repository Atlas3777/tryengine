#pragma once

#include "rg/RenderGraph.hpp"

namespace tryengine::graphics {

struct BlitPassData {
    RGResourceHandle src;
    RGResourceHandle dst;
};

inline void AddBlitPass(RenderGraph& rg, RGResourceHandle src_handle,
                 RGResourceHandle dst_handle,
    uint32_t src_w, uint32_t src_h,
    uint32_t dst_w, uint32_t dst_h
) {
    rg.AddPass<BlitPassData>(
        "BlitPass",
        [src_handle, dst_handle](RenderGraphBuilder& builder, BlitPassData& data) {
            data.src = builder.Read(src_handle, RGUsageHint::TransferSrc);
            data.dst = builder.Write(dst_handle, RGUsageHint::TransferDst);
            builder.MarkSideEffect();
        },
        [src_w, src_h, dst_w, dst_h](RGExecuteContext& ctx, const BlitPassData& data) {
            VkImage src_image = ctx.GetTexture(data.src);
            VkImage dst_image = ctx.GetTexture(data.dst);

            if (!src_image || !dst_image) {
                return;
            }

            VkImageBlit2 blit_region{};
            blit_region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
            blit_region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit_region.srcSubresource.layerCount = 1;
            blit_region.srcOffsets[0] = {0, 0, 0};
            blit_region.srcOffsets[1] = {static_cast<int32_t>(src_w), static_cast<int32_t>(src_h), 1};

            blit_region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit_region.dstSubresource.layerCount = 1;
            blit_region.dstOffsets[0] = {0, 0, 0};
            blit_region.dstOffsets[1] = {static_cast<int32_t>(dst_w), static_cast<int32_t>(dst_h), 1};

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
        });
};
}