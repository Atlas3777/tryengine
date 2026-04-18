#pragma once

#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_log.h>
#include <cereal/archives/binary.hpp>
#include <cereal/archives/json.hpp>
#include <fstream>
#include <memory>
#include <string>

#include "engine/core/ResourceManager.hpp"
#include "engine/graphics/MaterialSystem.hpp"
#include "engine/graphics/Types.hpp"
#include "engine/resources/MaterialAssetData.hpp"

namespace tryengine::graphics {

class ShaderAssetLoader {
public:
    using result_type = std::shared_ptr<Shader>;

    explicit ShaderAssetLoader(core::ResourceManager& rm, SDL_GPUDevice* device)
        : device_(device), resource_manager_(rm) {}

    result_type operator()(uint64_t id, const std::string& path) const {
        std::ifstream is(path, std::ios::binary);  // Обязательно binary мода
        if (!is.is_open()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShaderLoader: cannot open %s", path.c_str());
            return nullptr;
        }

        ShaderAsset asset;
        try {
            cereal::BinaryInputArchive archive(is);  // Читаем бинарник
            archive(asset);
        } catch (const std::exception& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ShaderLoader: parse error in %s: %s", path.c_str(), e.what());
            return nullptr;
        }

        auto shader = std::make_shared<Shader>();

        size_t fragmentCodeSize;

        auto path_to_fragment = resource_manager_.GetAssetDatabase().GetPath(asset.fragment_shader_id);

        void* fragmentCode = SDL_LoadFile(path_to_fragment.c_str(), &fragmentCodeSize);

        SDL_GPUShaderCreateInfo fragmentInfo{};
        fragmentInfo.code = (Uint8*) fragmentCode;
        fragmentInfo.code_size = fragmentCodeSize;
        fragmentInfo.entrypoint = "main";
        fragmentInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fragmentInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fragmentInfo.num_samplers = 1;
        fragmentInfo.num_storage_buffers = 0;
        fragmentInfo.num_storage_textures = 0;
        fragmentInfo.num_uniform_buffers = 1;

        SDL_GPUShader* fragmentShader = SDL_CreateGPUShader(device_, &fragmentInfo);
        SDL_free(fragmentCode);

        size_t vertexCodeSize;

        auto path_to_vertex = resource_manager_.GetAssetDatabase().GetPath(asset.vertex_shader_id);

        void* vertexCode = SDL_LoadFile(path_to_vertex.c_str(), &vertexCodeSize);

        SDL_GPUShaderCreateInfo vertexInfo;
        vertexInfo.code = (Uint8*) vertexCode;
        vertexInfo.code_size = vertexCodeSize;
        vertexInfo.entrypoint = "main";
        vertexInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vertexInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vertexInfo.num_samplers = 0;
        vertexInfo.num_storage_buffers = 0;
        vertexInfo.num_storage_textures = 0;
        vertexInfo.num_uniform_buffers = 1;

        SDL_GPUShader* vertexShader = SDL_CreateGPUShader(device_, &vertexInfo);
        SDL_free(vertexCode);

        shader->fragment_shader = fragmentShader;
        shader->vertex_shader = vertexShader;

        // 2. Формируем Runtime Layout
        for (const auto& p : asset.params) {
            shader->layout.AddParam(p.name, p.type);
        }
        for (const auto& t : asset.textures) {
            shader->layout.texture_slots[t.name] = t.slot;
        }

        // 3. Подготавливаем дефолтный буфер
        shader->default_uniform_data.assign(shader->layout.uniform_buffer_size, 0);
        for (const auto& p : asset.params) {
            auto it = std::find_if(shader->layout.params.begin(), shader->layout.params.end(),
                                   [&](auto& info) { return info.name == p.name; });
            if (it != shader->layout.params.end() && !p.default_values.empty()) {
                std::memcpy(shader->default_uniform_data.data() + it->offset, p.default_values.data(),
                            std::min((size_t) it->size, p.default_values.size() * sizeof(float)));
            }
        }
        return shader;
    }

private:
    SDL_GPUDevice* device_;
    core::ResourceManager& resource_manager_;
};

}  // namespace tryengine::graphics