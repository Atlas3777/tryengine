#pragma once

#include <SDL3/SDL_gpu.h>
#include <algorithm>
#include <memory>

#include "editor/JsonParser.hpp"
#include "engine/graphics/AssetTypes.hpp"
#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/resources/ResourceManager.hpp"

namespace tryengine::graphics {

class ShaderLoader {
public:
    explicit ShaderLoader(resources::ResourceManager& rm, SDL_GPUDevice* device)
        : device_(device), resource_manager_(rm) {}

    async::Task<Shader> Parse(eastl::span<const uint8_t> data) const {
        co_await tryengine::async::ExecutorSwitch(tryengine::async::MainThread());

        auto res = tryeditor::Deserialize<ShaderAsset>(data);

        if (!res.has_value())
            co_return core::Error("Deserialize of ShaderAsset failed");

        ShaderAsset& asset = *res;

        size_t fragment_code_size;

        auto path_to_fragment = resource_manager_.GetAssetRegistry().GetArtifactPath(asset.fragment_shader_id);

        if (!path_to_fragment.has_value())
            co_return LogAndMakeError("path for guid {} not found", asset.fragment_shader_id);


        void* fragment_code = SDL_LoadFile(path_to_fragment->c_str(), &fragment_code_size);


        SDL_GPUShaderCreateInfo fragment_info{};
        fragment_info.code = (Uint8*) fragment_code;
        fragment_info.code_size = fragment_code_size;
        fragment_info.entrypoint = "main";
        fragment_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fragment_info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fragment_info.num_samplers = 1;
        fragment_info.num_storage_buffers = 1;
        fragment_info.num_storage_textures = 0;
        fragment_info.num_uniform_buffers = 1;

        SDL_GPUShader* fragment_shader = SDL_CreateGPUShader(device_, &fragment_info);
        SDL_free(fragment_code);

        size_t vertex_code_size;

        auto path_to_vertex = resource_manager_.GetAssetRegistry().GetArtifactPath(asset.vertex_shader_id);

        if (!path_to_vertex.has_value())
            co_return LogAndMakeError("path for guid {} not found", asset.vertex_shader_id);


        void* vertex_code = SDL_LoadFile(path_to_vertex->c_str(), &vertex_code_size);

        SDL_GPUShaderCreateInfo vertex_info;
        vertex_info.code = (Uint8*) vertex_code;
        vertex_info.code_size = vertex_code_size;
        vertex_info.entrypoint = "main";
        vertex_info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vertex_info.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vertex_info.num_samplers = 0;
        vertex_info.num_storage_buffers = 0;
        vertex_info.num_storage_textures = 0;
        vertex_info.num_uniform_buffers = 1;

        SDL_GPUShader* vertex_shader = SDL_CreateGPUShader(device_, &vertex_info);
        SDL_free(vertex_code);

        if (fragment_shader == nullptr) {
            co_return core::Error("Could not load vertex shader");
        }
        if (vertex_shader == nullptr) {
            co_return core::Error("Could not load vertex shader");
        }

        Shader shader;
        shader.fragment_shader = fragment_shader;
        shader.vertex_shader = vertex_shader;

        // 2. Формируем Runtime Layout
        for (const auto& p : asset.params) {
            shader.layout.AddParam(p.name, p.type);
        }
        for (const auto& [name, slot] : asset.textures) {
            shader.layout.texture_slots[name] = slot;
        }

        // 3. Подготавливаем дефолтный буфер
        shader.default_uniform_data.assign(shader.layout.uniform_buffer_size, 0);
        for (const auto& p : asset.params) {
            auto it = std::find_if(shader.layout.params.begin(), shader.layout.params.end(),
                                   [&](auto& info) { return info.name == p.name; });

            if (it != shader.layout.params.end() && !p.default_values.empty()) {
                std::memcpy(shader.default_uniform_data.data() + it->offset, p.default_values.data(),
                            std::min((size_t) it->size, p.default_values.size() * sizeof(float)));
            }
        }
        co_return shader;
    }

private:
    SDL_GPUDevice* device_;
    resources::ResourceManager& resource_manager_;
};

}  // namespace tryengine::graphics