#pragma once

#include <SDL3/SDL_gpu.h>

namespace trygame {

inline void RenderToSwapchain(
    SDL_GPUCommandBuffer* cmd,
    SDL_GPUTexture* src, uint32_t src_w, uint32_t src_h,
    SDL_GPUTexture* dst, uint32_t dst_w, uint32_t dst_h)
{
    SDL_GPUBlitInfo blit_info{};

    // Источник
    blit_info.source.texture = src;
    blit_info.source.x = 0;
    blit_info.source.y = 0;
    blit_info.source.w = src_w;
    blit_info.source.h = src_h;
    blit_info.source.mip_level = 0;
    blit_info.source.layer_or_depth_plane = 0;

    // Назначение (Swapchain)
    blit_info.destination.texture = dst;
    blit_info.destination.x = 0;
    blit_info.destination.y = 0;
    blit_info.destination.w = dst_w;
    blit_info.destination.h = dst_h;
    blit_info.destination.mip_level = 0;
    blit_info.destination.layer_or_depth_plane = 0;

    blit_info.load_op = SDL_GPU_LOADOP_DONT_CARE;
    blit_info.filter = SDL_GPU_FILTER_NEAREST;
    blit_info.flip_mode = SDL_FLIP_NONE;
    blit_info.cycle = false;

    SDL_BlitGPUTexture(cmd, &blit_info);

    // Отправляем буфер команд на исполнение GPU
    SDL_SubmitGPUCommandBuffer(cmd);
}

}  // namespace trygame