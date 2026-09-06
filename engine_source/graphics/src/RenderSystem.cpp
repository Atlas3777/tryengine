#include "engine/graphics/RenderSystem.hpp"

#include <algorithm>
#include <cstring>
#include <hlsl++.h>

namespace tryengine::graphics {

struct LightUploadPassData {
    RGResourceHandle point_light_buffer;
    RGResourceHandle global_light_buffer;
    eastl::span<const PointLightGPU> lights;
    GlobalLightUniforms global_light;
};

struct ForwardPassData {
    RGResourceHandle color_target;
    RGResourceHandle depth_target;
    RGResourceHandle point_light_buffer;
    RGResourceHandle global_light_buffer;
    eastl::span<const DrawCommand> draws;
    CameraData* camera = nullptr;
    hlslpp::float4 clear_color;
};

RenderSystem::RenderSystem(SDL_GPUDevice* device)
    : device_(device), render_graph_(device) {
    pipeline_manager_ = std::make_unique<PipelineManager>(device);
}

RenderSystem::~RenderSystem() = default;

void RenderSystem::ClearAllPasses() {
    for (auto& pass : pass_queues_) pass->Clear();
}

void RenderSystem::RenderToTarget(SDL_GPUCommandBuffer* cmd_buffer,
                                    RenderTarget& target,
                                    CameraData& camera,
                                    const AmbientSettings& ambient,
                                    eastl::span<const PointLightGPU> point_lights) {
    render_graph_.Reset();

    RGTextureDesc color_desc{
        .width = target.GetWidth(),
        .height = target.GetHeight(),
        .format = target.GetColorFormat(),
        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET
    };
    RGTextureDesc depth_desc{
        .width = target.GetWidth(),
        .height = target.GetHeight(),
        .format = target.GetDepthFormat(),
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
    };

    RGResourceHandle color_handle = render_graph_.ImportExternalTexture("ColorTarget", target.GetColor(), color_desc);
    RGResourceHandle depth_handle = render_graph_.ImportExternalTexture("DepthTarget", target.GetDepth(), depth_desc);

    render_graph_.AddPass<LightUploadPassData>(
        "LightUpload",
        /* Setup */ [&](RenderGraphBuilder& b, LightUploadPassData& data) {
            data.lights = point_lights;
            data.global_light.ambient_color = ambient.ambient_color;
            data.global_light.view_pos = hlslpp::float4(camera.position.x, camera.position.y, camera.position.z, 1.0f);

            if (!data.lights.empty()) {
                RGBufferDesc point_desc{
                    .size = sizeof(PointLightGPU) * data.lights.size(),
                    .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                    .debug_name = "PointLightSSBO"
                };
                data.point_light_buffer = b.CreateBuffer("PointLightSSBO", point_desc);
                b.Export(RGTag_v<"PointLightBuffer">, data.point_light_buffer);
            }

            RGBufferDesc global_desc{
                .size = sizeof(GlobalLightUniforms),
                .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
                .debug_name = "GlobalLightUBO"
            };
            data.global_light_buffer = b.CreateBuffer("GlobalLightUBO", global_desc);
            b.Export(RGTag_v<"GlobalLightUBO">, data.global_light_buffer);
        },
        /* Execute */ [](RGExecuteContext& ctx, const LightUploadPassData& data) {
            // Загрузка глобального освещения
            if (data.global_light_buffer.IsValid()) {
                SDL_GPUBuffer* dst_buffer = ctx.GetBuffer(data.global_light_buffer);
                if (dst_buffer) {
                    SDL_GPUTransferBufferCreateInfo xfer_info{};
                    xfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                    xfer_info.size = sizeof(GlobalLightUniforms);
                    SDL_GPUTransferBuffer* xfer_buffer = SDL_CreateGPUTransferBuffer(ctx.device, &xfer_info);

                    void* mapped_data = SDL_MapGPUTransferBuffer(ctx.device, xfer_buffer, false);
                    std::memcpy(mapped_data, &data.global_light, sizeof(GlobalLightUniforms));
                    SDL_UnmapGPUTransferBuffer(ctx.device, xfer_buffer);

                    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(ctx.cmd_buffer);
                    SDL_GPUTransferBufferLocation src{xfer_buffer, 0};
                    SDL_GPUBufferRegion dst{dst_buffer, 0, xfer_info.size};

                    SDL_UploadToGPUBuffer(copy_pass, &src, &dst, true);
                    SDL_EndGPUCopyPass(copy_pass);

                    SDL_ReleaseGPUTransferBuffer(ctx.device, xfer_buffer);
                }
            }

            // Загрузка точечных источников света
            if (!data.lights.empty() && data.point_light_buffer.IsValid()) {
                SDL_GPUBuffer* dst_buffer = ctx.GetBuffer(data.point_light_buffer);
                if (dst_buffer) {
                    SDL_GPUTransferBufferCreateInfo xfer_info{};
                    xfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
                    xfer_info.size = static_cast<uint32_t>(sizeof(PointLightGPU) * data.lights.size());
                    SDL_GPUTransferBuffer* xfer_buffer = SDL_CreateGPUTransferBuffer(ctx.device, &xfer_info);

                    void* mapped_data = SDL_MapGPUTransferBuffer(ctx.device, xfer_buffer, false);
                    std::memcpy(mapped_data, data.lights.data(), xfer_info.size);
                    SDL_UnmapGPUTransferBuffer(ctx.device, xfer_buffer);

                    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(ctx.cmd_buffer);
                    SDL_GPUTransferBufferLocation src{xfer_buffer, 0};
                    SDL_GPUBufferRegion dst{dst_buffer, 0, xfer_info.size};

                    SDL_UploadToGPUBuffer(copy_pass, &src, &dst, true);
                    SDL_EndGPUCopyPass(copy_pass);

                    SDL_ReleaseGPUTransferBuffer(ctx.device, xfer_buffer);
                }
            }
        });

    for (auto& pass_queue : pass_queues_) {
        if (pass_queue->IsEmpty()) continue;

        render_graph_.AddPass<ForwardPassData>(
            pass_queue->GetName(),
            /* Setup */ [&](RenderGraphBuilder& b, ForwardPassData& data) {
                data.draws = pass_queue->GetCommands();
                data.camera = &camera;
                data.clear_color = ambient.clear_color;

                data.color_target = b.Write(color_handle);
                data.depth_target = b.Write(depth_handle);
                data.point_light_buffer = b.Import(RGTag_v<"PointLightBuffer">);
                data.global_light_buffer = b.Import(RGTag_v<"GlobalLightUBO">);

                b.MarkSideEffect();
            },
            /* Execute */ [](RGExecuteContext& ctx, const ForwardPassData& data) {
                SDL_GPUColorTargetInfo color_info{};
                color_info.texture = ctx.GetTexture(data.color_target);
                color_info.clear_color = {data.clear_color.r, data.clear_color.g, data.clear_color.b, data.clear_color.a};
                color_info.load_op = SDL_GPU_LOADOP_CLEAR;
                color_info.store_op = SDL_GPU_STOREOP_STORE;

                SDL_GPUDepthStencilTargetInfo depth_info{};
                depth_info.texture = ctx.GetTexture(data.depth_target);
                depth_info.clear_depth = 1.0f;
                depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
                depth_info.store_op = SDL_GPU_STOREOP_STORE;

                SDL_GPURenderPass* scene_pass = SDL_BeginGPURenderPass(ctx.cmd_buffer, &color_info, 1, &depth_info);
                ctx.gpu_pass = scene_pass;

                ExecuteDrawCommands(ctx, data.camera, data.draws);

                SDL_EndGPURenderPass(scene_pass);
            });
    }

    render_graph_.Compile();
    render_graph_.Execute(cmd_buffer);
}

}  // namespace tryengine::graphics