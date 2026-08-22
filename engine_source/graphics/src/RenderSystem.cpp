#include "engine/graphics/RenderSystem.hpp"

#include <algorithm>
#include <cstring>
#include <hlsl++.h>

namespace tryengine::graphics {

RenderSystem::RenderSystem(SDL_GPUDevice* device) : device_(device) {
    pipeline_manager_ = std::make_unique<PipelineManager>(device);
}

RenderSystem::~RenderSystem() {
    if (light_storage_buffer_) {
        SDL_ReleaseGPUBuffer(device_, light_storage_buffer_);
    }
}

constexpr SDL_GPUIndexElementSize IndexFormat(const resources::IndexFormat format) {
    return format == resources::IndexFormat::UInt32 ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT;
}

void RenderSystem::RenderToTarget(SDL_GPUCommandBuffer* cmd_buffer, RenderTarget& target, CameraData& camera) {
    if (!lights_queue_.empty()) {
        if (!light_storage_buffer_ || current_buffer_capacity_ < lights_queue_.size()) {
            if (light_storage_buffer_) {
                SDL_ReleaseGPUBuffer(device_, light_storage_buffer_);
            }
            current_buffer_capacity_ = std::max(static_cast<size_t>(64), lights_queue_.size());

            SDL_GPUBufferCreateInfo buffer_info{};
            buffer_info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
            buffer_info.size = sizeof(PointLightGPU) * current_buffer_capacity_;
            light_storage_buffer_ = SDL_CreateGPUBuffer(device_, &buffer_info);
        }

        SDL_GPUTransferBufferCreateInfo xfer_info{};
        xfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        xfer_info.size = sizeof(PointLightGPU) * lights_queue_.size();
        SDL_GPUTransferBuffer* xfer_buffer = SDL_CreateGPUTransferBuffer(device_, &xfer_info);

        void* mapped_data = SDL_MapGPUTransferBuffer(device_, xfer_buffer, false);
        std::memcpy(mapped_data, lights_queue_.data(), xfer_info.size);
        SDL_UnmapGPUTransferBuffer(device_, xfer_buffer);

        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd_buffer);
        SDL_GPUTransferBufferLocation src{xfer_buffer, 0};
        SDL_GPUBufferRegion dst{light_storage_buffer_, 0, xfer_info.size};

        SDL_UploadToGPUBuffer(copy_pass, &src, &dst, true);
        SDL_EndGPUCopyPass(copy_pass);

        SDL_ReleaseGPUTransferBuffer(device_, xfer_buffer);
    }

    SDL_GPUColorTargetInfo color_info{};
    color_info.texture = target.GetColor();
    color_info.clear_color = {ambient.clear_color.r, ambient.clear_color.g, ambient.clear_color.b,
                              ambient.clear_color.a};
    color_info.load_op = SDL_GPU_LOADOP_CLEAR;
    color_info.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPUDepthStencilTargetInfo depth_info{};
    depth_info.texture = target.GetDepth();
    depth_info.clear_depth = 1.0f;
    depth_info.load_op = SDL_GPU_LOADOP_CLEAR;
    depth_info.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* scene_pass = SDL_BeginGPURenderPass(cmd_buffer, &color_info, 1, &depth_info);

    if (draw_queue_.empty()) {
        SDL_EndGPURenderPass(scene_pass);
        return;
    }

    std::sort(draw_queue_.begin(), draw_queue_.end(),
              [](const DrawCommand& a, const DrawCommand& b) { return a.sorting_key < b.sorting_key; });

    GlobalLightUniforms global_light_data;
    global_light_data.ambient_color = ambient.ambient_color;
    global_light_data.view_pos = hlslpp::float4(camera.position.x, camera.position.y,camera.position.z, 1.0f);

    SDL_PushGPUFragmentUniformData(cmd_buffer, 0, &global_light_data, sizeof(GlobalLightUniforms));

    if (light_storage_buffer_) {
        SDL_BindGPUFragmentStorageBuffers(scene_pass, 0, &light_storage_buffer_, 1);
    }

    SDL_GPUGraphicsPipeline* current_pipeline = nullptr;
    Material* current_material = nullptr;
    SDL_GPUBuffer* current_vertex_buffer = nullptr;
    SDL_GPUBuffer* current_index_buffer = nullptr;

    for (const auto& command : draw_queue_) {
        if (!command.pipeline || !command.vertex_buffer || !command.material)
            continue;

        if (command.pipeline != current_pipeline) {
            SDL_BindGPUGraphicsPipeline(scene_pass, command.pipeline);
            current_pipeline = command.pipeline;
            current_material = nullptr;
            current_vertex_buffer = nullptr;
            current_index_buffer = nullptr;
        }

        struct alignas(16) CombinedUBO {
            hlslpp::float4x4 view;
            hlslpp::float4x4 proj;
            hlslpp::float4x4 model;
            hlslpp::float4x4 normalMatrix;
        } ubo{};
        ubo.view = camera.view;
        ubo.proj = camera.proj;
        ubo.model = command.model_matrix;
        ubo.normalMatrix = hlslpp::transpose(hlslpp::inverse(ubo.model));
        SDL_PushGPUVertexUniformData(cmd_buffer, 0, &ubo, sizeof(CombinedUBO));

        if (command.material != current_material) {
            const Shader& shader = command.material->shader();

            if (shader.layout.uniform_buffer_size > 0) {
                SDL_PushGPUFragmentUniformData(cmd_buffer, shader.layout.uniform_binding_slot,
                                               command.material->uniform_buffer.data(),
                                               shader.layout.uniform_buffer_size);
            }

            for (const auto& texture : command.material->textures) {
                TRY_CHECK(texture->sampler != nullptr, "Material bound with null sampler");
                TRY_CHECK(texture->texture != nullptr, "Material bound with null texture");

                SDL_GPUTextureSamplerBinding tsb{texture->texture, texture->sampler};
                SDL_BindGPUFragmentSamplers(scene_pass, texture->slot, &tsb, 1);
            }
            current_material = command.material;
        }

        if (command.vertex_buffer != current_vertex_buffer) {
            SDL_GPUBufferBinding vb = {command.vertex_buffer, 0};
            SDL_BindGPUVertexBuffers(scene_pass, 0, &vb, 1);
            current_vertex_buffer = command.vertex_buffer;
        }

        if (command.index_buffer != current_index_buffer) {
            SDL_GPUBufferBinding ib = {command.index_buffer, 0};
            SDL_GPUIndexElementSize index_size = IndexFormat(command.index_format);

            SDL_BindGPUIndexBuffer(scene_pass, &ib, index_size);
            current_index_buffer = command.index_buffer;
        }

        SDL_DrawGPUIndexedPrimitives(scene_pass, command.num_indices, 1, 0, 0, 0);
    }

    SDL_EndGPURenderPass(scene_pass);
}

}  // namespace tryengine::graphics