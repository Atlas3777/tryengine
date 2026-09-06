#pragma once

#include <SDL3/SDL_gpu.h>
#include <algorithm>

#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/graphics/ShaderBinary.hpp"
#include "engine/async/Task.hpp"

namespace tryengine::graphics {

class ShaderLoader {
public:
    explicit ShaderLoader(SDL_GPUDevice* device) : device_(device) {}

    async::Task<Shader> Parse(eastl::span<const uint8_t> data) const {
        co_await tryengine::async::ExecutorSwitch(tryengine::async::MainThread());

        auto unpack_result = UnpackShaderBinary(data);
        if (!unpack_result.has_value()) {
            co_return LogAndMakeError("Failed to unpack ShaderBinary container");
        }
        auto& binary_content = *unpack_result;

        // 1. Вертексный шейдер
        SDL_GPUShaderCreateInfo vertex_info{};
        vertex_info.code = binary_content.vertex_spv.data();
        vertex_info.code_size = binary_content.vertex_spv.size();
        vertex_info.entrypoint = "vsMain";
        vertex_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vertex_info.num_samplers = binary_content.vertex_counts.num_samplers;
        vertex_info.num_storage_buffers = binary_content.vertex_counts.num_storage_buffers;
        vertex_info.num_storage_textures = binary_content.vertex_counts.num_storage_textures;
        vertex_info.num_uniform_buffers = binary_content.vertex_counts.num_uniform_buffers;

        SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(device_, &vertex_info);

        // 2. Фрагментный шейдер
        SDL_GPUShaderCreateInfo fragment_info{};
        fragment_info.code = binary_content.fragment_spv.data();
        fragment_info.code_size = binary_content.fragment_spv.size();
        fragment_info.entrypoint = "fsMain";
        fragment_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fragment_info.num_samplers = binary_content.fragment_counts.num_samplers;
        fragment_info.num_storage_buffers = binary_content.fragment_counts.num_storage_buffers;
        fragment_info.num_storage_textures = binary_content.fragment_counts.num_storage_textures;
        fragment_info.num_uniform_buffers = binary_content.fragment_counts.num_uniform_buffers;

        SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(device_, &fragment_info);

        if (!vertex_shader || !fragment_shader) {
            if (vertex_shader) SDL_ReleaseGPUShader(device_, vertex_shader);
            if (fragment_shader) SDL_ReleaseGPUShader(device_, fragment_shader);
            co_return core::Error("Could not create SDL_GPUShader from SPIR-V binary");
        }

        Shader shader{};
        shader.vertex_shader = vertex_shader;
        shader.fragment_shader = fragment_shader;
        shader.reflection = std::move(binary_content.reflection);

        co_return shader;
    }

private:
    SDL_GPUDevice* device_ = nullptr;
};

}  // namespace tryengine::graphics