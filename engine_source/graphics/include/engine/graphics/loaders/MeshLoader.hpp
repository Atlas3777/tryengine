#pragma once

#include "engine/async/GlobalExecutors.hpp"
#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/resources/MeshBinary.hpp"

namespace tryengine::graphics {

class MeshLoader {
public:
    explicit MeshLoader(SDL_GPUDevice* device) : device(device) {}

    async::Task<Mesh> Parse(eastl::span<const uint8_t> raw_bytes) {
        auto unpack_res = resources::MeshBinary::Unpack(raw_bytes);
        if (!unpack_res.has_value()) {
            co_return LogAndMakeError("Failed to load mesh: {}", unpack_res.error().Message());
        }

        const auto& [header, vertex_data, index_data] = *unpack_res;

        Mesh mesh;
        mesh.v_format = header.vertex_format;
        mesh.i_format = header.index_format;
        mesh.num_indices = index_data.size_bytes() / resources::GetIndexStride(mesh.i_format);

        co_await async::ExecutorSwitch{async::MainThread()};

        SDL_GPUBufferCreateInfo vertex_info;
        vertex_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
        vertex_info.size = vertex_data.size_bytes();

        mesh.vertex_buffer = SDL_CreateGPUBuffer(device, &vertex_info);

        SDL_GPUBufferCreateInfo index_info;
        index_info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
        index_info.size = index_data.size_bytes();

        mesh.index_buffer = SDL_CreateGPUBuffer(device, &index_info);

        SDL_GPUTransferBufferCreateInfo transfer_buffer_create_info{};
        transfer_buffer_create_info.size = vertex_data.size_bytes() + index_data.size_bytes();
        transfer_buffer_create_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        SDL_GPUTransferBuffer* transfer_buffer = SDL_CreateGPUTransferBuffer(device, &transfer_buffer_create_info);

        auto* ptr = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(device, transfer_buffer, false));
        memcpy(ptr, vertex_data.data(), vertex_data.size_bytes());
        memcpy(ptr + vertex_data.size_bytes(), index_data.data(), index_data.size_bytes());
        SDL_UnmapGPUTransferBuffer(device, transfer_buffer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);

        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);

        const SDL_GPUTransferBufferLocation transferBuffer{transfer_buffer, 0};
        const SDL_GPUBufferRegion bufferReg{mesh.vertex_buffer, 0, (Uint32)vertex_data.size_bytes()};
        SDL_UploadToGPUBuffer(copy, &transferBuffer, &bufferReg, false);

        const SDL_GPUTransferBufferLocation transferBuffer2{transfer_buffer, (Uint32)vertex_data.size_bytes()};
        const SDL_GPUBufferRegion bufferReg2{mesh.index_buffer, 0, (Uint32)index_data.size_bytes()};
        SDL_UploadToGPUBuffer(copy, &transferBuffer2, &bufferReg2, false);

        SDL_EndGPUCopyPass(copy);

        SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(device, transfer_buffer);

        co_return mesh;
    }

private:
    SDL_GPUDevice* device;
};

}  // namespace tryengine::graphics