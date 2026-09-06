#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <SDL3/SDL_gpu.h>
#include <cstring>
#include <algorithm>

#include "engine/core/Assert.hpp"
#include "engine/graphics/ShaderReflection.hpp"
#include "engine/resources/ResourceHandle.hpp"
#include "engine/resources/Vertex.hpp"

namespace tryengine::graphics {

struct Shader {
    SDL_GPUShader* vertex_shader = nullptr;
    SDL_GPUShader* fragment_shader = nullptr;

    ShaderReflectionData reflection;
    eastl::vector<uint8_t> default_uniform_data;
};

struct TextureSampler {
    SDL_GPUTexture* texture = nullptr;
    SDL_GPUSampler* sampler = nullptr;
};

struct MaterialTextureBinding {
    uint32_t slot = 0;
    resources::ResourceHandle<TextureSampler> texture;
};

struct Mesh {
    SDL_GPUBuffer* vertex_buffer = nullptr;
    SDL_GPUBuffer* index_buffer = nullptr;
    uint32_t num_indices = 0;
    resources::VertexFormat v_format = resources::VertexFormat::Standard;
    resources::IndexFormat i_format = resources::IndexFormat::UInt16;
};

struct Material {
    explicit Material(resources::ResourceHandle<Shader> shdr_handle)
        : shader_handle(std::move(shdr_handle)) {

        size_t ubo_size = 0;
        // Находим размер UBO материала из рефлексии (первый нерезервированный UBO)
        shader().reflection.ForEachMaterialBinding([&ubo_size](const ShaderReflectedBinding& b) {
            if (b.kind == ShaderResourceKind::UniformBuffer) {
                ubo_size = std::max<size_t>(ubo_size, b.size);
            }
        });

        uniform_buffer.assign(ubo_size, 0);

        if (!shader().default_uniform_data.empty()) {
            std::memcpy(uniform_buffer.data(), shader().default_uniform_data.data(),
                        std::min(uniform_buffer.size(), shader().default_uniform_data.size()));
        }
    }

    resources::ResourceHandle<Shader> shader_handle;
    eastl::vector<uint8_t> uniform_buffer;
    eastl::vector<MaterialTextureBinding> textures;

    Shader& shader() const { return *shader_handle; }

    void SetTexture(uint32_t slot, resources::ResourceHandle<TextureSampler> tex) {
        for (auto& bind : textures) {
            if (bind.slot == slot) {
                bind.texture = std::move(tex);
                return;
            }
        }
        textures.push_back({slot, std::move(tex)});
    }

    void SetTexture(eastl::string_view name, resources::ResourceHandle<TextureSampler> tex) {
        const auto* binding = shader().reflection.FindBinding(name);
        if (binding) {
            SetTexture(binding->binding, std::move(tex));
        } else {
            core::LogInfo("Material::SetTexture: Texture binding '{}' not found in shader reflection", name.data());
        }
    }

    template <typename T>
    void SetParam(eastl::string_view name, const T& value) {
        SetParamRaw(name, &value, sizeof(T));
    }

    void SetParamRaw(eastl::string_view name, const void* data_ptr, uint32_t data_size) {
        TRY_ASSERT(data_ptr, "data_ptr is null");
        RGTag tag = RGTagOf(name);

        shader().reflection.ForEachMaterialBinding([this, tag, data_ptr, data_size](const ShaderReflectedBinding& b) {
            if (b.kind == ShaderResourceKind::UniformBuffer) {
                const auto* param = b.FindParamByTag(tag);
                if (param && param->offset + data_size <= uniform_buffer.size()) {
                    std::memcpy(uniform_buffer.data() + param->offset, data_ptr, data_size);
                }
            }
        });
    }
};

}  // namespace tryengine::graphics