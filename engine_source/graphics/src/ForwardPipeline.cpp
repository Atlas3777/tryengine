#include "engine/graphics/ForwardPipeline.hpp"

#include <algorithm>
#include <cstring>

#include "engine/graphics/OpaqueGeometryPass.hpp"

namespace tryengine::graphics {

FrameRenderData CollectFrameRenderData(core::Engine& engine, PipelineManager& pm) {
    FrameRenderData data;
    data.point_lights = eastl::move(CollectLight(engine));
    data.opaque_queue = eastl::move(OpaqueGeometryPass::CollectDrawable(engine, pm));
    return data;
}

void SetupFrameConstants(RenderGraph& rg, const CameraData& camera) {
    struct LightUBO {
        hlslpp::float4 am_color{0.1f, 0.1f, 0.1f, 0.0f};
        hlslpp::float4 view_pos;
    } light_ubo;
    light_ubo.view_pos = hlslpp::float4(camera.position, 0.0f);

    rg.PutCPUData(RGTag_v<"GlobalLightUBO">, light_ubo);
    rg.PutCPUData(RGTag_v<"Camera">, camera);
}

RGResourceHandle AddLightPass(RenderGraph& rg, eastl::span<const PointLightGPU> point_lights) {
    auto& light_data = rg.AddPass<LightPass>(
        "LightPass",
        [point_lights](RenderGraphBuilder& builder, LightPass& data) {
            data.point_lights_queue = point_lights;

            RGBufferDesc desc;
            desc.debug_name = "PointLightBuffer";
            desc.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
            size_t element_count = std::max<size_t>(1, data.point_lights_queue.size());
            desc.size = sizeof(PointLightGPU) * element_count;

            data.buffer = builder.CreateBuffer("PointLightBuffer", desc);
            builder.Export(RGTag_v<"PointLightBuffer">, data.buffer);
        },
        [](RGExecuteContext& ctx, const LightPass& data) {
            if (data.point_lights_queue.empty())
                return;

            SDL_GPUBuffer* dst_buffer = ctx.GetBuffer(data.buffer);
            if (!dst_buffer)
                return;

            uint32_t upload_size =
                static_cast<uint32_t>(sizeof(PointLightGPU) * data.point_lights_queue.size());

            SDL_GPUTransferBufferCreateInfo xfer_info{};
            xfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            xfer_info.size = upload_size;
            SDL_GPUTransferBuffer* xfer_buffer = SDL_CreateGPUTransferBuffer(ctx.device, &xfer_info);

            void* mapped_data = SDL_MapGPUTransferBuffer(ctx.device, xfer_buffer, false);
            std::memcpy(mapped_data, data.point_lights_queue.data(), upload_size);
            SDL_UnmapGPUTransferBuffer(ctx.device, xfer_buffer);

            SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(ctx.cmd_buffer);
            SDL_GPUTransferBufferLocation src{xfer_buffer, 0};
            SDL_GPUBufferRegion dst{dst_buffer, 0, upload_size};

            SDL_UploadToGPUBuffer(copy_pass, &src, &dst, true);
            SDL_EndGPUCopyPass(copy_pass);

            SDL_ReleaseGPUTransferBuffer(ctx.device, xfer_buffer);
        });

    return light_data.buffer;
}

ForwardPipelineOutput BuildForwardPipeline(
    RenderGraph& rg,
    const FrameRenderData& frame_data,
    uint32_t width,
    uint32_t height
) {
    ForwardPipelineOutput out;

    out.light_buffer = AddLightPass(rg, frame_data.point_lights);

    auto& forward_data = rg.AddPass<OpaquePass>(
        "OpaquePass",
        [&, width, height](RenderGraphBuilder& builder, OpaquePass& data) {
            RGTextureDesc scene_color_desc{
                width, height, 1, 1,
                SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
                SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
                "SceneColor"};

            RGTextureDesc scene_depth_desc{
                width, height, 1, 1,
                SDL_GPU_TEXTUREFORMAT_D16_UNORM,
                SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
                "SceneDepth"};

            RGResourceHandle color_rt = builder.CreateTexture("SceneColor", scene_color_desc);
            RGResourceHandle depth_rt = builder.CreateTexture("SceneDepth", scene_depth_desc);

            data.color_target = builder.Write(color_rt);
            data.depth_target = builder.Write(depth_rt);
            data.light_buffer = builder.Read(out.light_buffer);

            data.opaque_pass_queue = eastl::span(frame_data.opaque_queue.data(), frame_data.opaque_queue.size());

            builder.Export(RGTag_v<"SceneViewport">, data.color_target);
        },
        [](RGExecuteContext& ctx, const OpaquePass& data) {
            SDL_GPUTexture* color_tex = ctx.GetTexture(data.color_target);
            SDL_GPUTexture* depth_tex = ctx.GetTexture(data.depth_target);

            SDL_GPUColorTargetInfo color_info{};
            color_info.texture = color_tex;
            color_info.clear_color = {0.1f, 0.1f, 0.12f, 1.0f};
            color_info.load_op = SDL_GPU_LOADOP_CLEAR;
            color_info.store_op = SDL_GPU_STOREOP_STORE;

            SDL_GPUDepthStencilTargetInfo depth_info{};
            depth_info.texture = depth_tex;
            depth_info.clear_depth = 1.0f;
            depth_info.clear_stencil = 0;
            depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
            depth_info.store_op = SDL_GPU_STOREOP_DONT_CARE;

            ctx.gpu_pass = SDL_BeginGPURenderPass(ctx.cmd_buffer, &color_info, 1, &depth_info);
            OpaqueGeometryPass::ExecuteDrawCommands(ctx, data.opaque_pass_queue);
            SDL_EndGPURenderPass(ctx.gpu_pass);
            ctx.gpu_pass = nullptr;
        });

    out.color_target = forward_data.color_target;
    out.depth_target = forward_data.depth_target;

    return out;
}

void AddBlitPass(
    RenderGraph& rg,
    RGResourceHandle src_handle,
    RGResourceHandle dst_handle,
    uint32_t src_w, uint32_t src_h,
    uint32_t dst_w, uint32_t dst_h
) {
    rg.AddPass<BlitPassData>(
        "BlitPass",
        [src_handle, dst_handle](RenderGraphBuilder& builder, BlitPassData& data) {
            data.src = builder.Read(src_handle, SDL_GPU_TEXTUREUSAGE_SAMPLER);
            data.dst = builder.Write(dst_handle, SDL_GPU_TEXTUREUSAGE_COLOR_TARGET);
            builder.MarkSideEffect();
        },
        [src_w, src_h, dst_w, dst_h](RGExecuteContext& ctx, const BlitPassData& data) {
            SDL_GPUTexture* src_tex = ctx.GetTexture(data.src);
            SDL_GPUTexture* dst_tex = ctx.GetTexture(data.dst);

            if (!src_tex || !dst_tex)
                return;

            SDL_GPUBlitInfo blit_info{};
            blit_info.source.texture = src_tex;
            blit_info.source.w = src_w;
            blit_info.source.h = src_h;

            blit_info.destination.texture = dst_tex;
            blit_info.destination.w = dst_w;
            blit_info.destination.h = dst_h;

            blit_info.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit_info.filter = SDL_GPU_FILTER_NEAREST;
            blit_info.flip_mode = SDL_FLIP_NONE;

            SDL_BlitGPUTexture(ctx.cmd_buffer, &blit_info);
        });
}

}  // namespace tryengine::graphics