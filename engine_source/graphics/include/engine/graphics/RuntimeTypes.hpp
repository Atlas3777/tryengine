#pragma once

#include <EASTL/hash_map.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <SDL3/SDL_gpu.h>
#include <algorithm>
#include <cstring>

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
    resources::VertexFormat v_format = resources::VertexFormat::StaticPacked;
    resources::IndexFormat i_format = resources::IndexFormat::UInt16;
};

// Информация о том, как запушить конкретный кусок UBO в SDL_gpu
struct CachedUBOBinding {
    uint32_t slot = 0;              // Binding slot в шейдере
    ShaderStage stage = ShaderStage::Fragment;
    uint32_t offset_in_buffer = 0;  // Смещение внутри Material::uniform_buffer
    uint32_t size = 0;              // Размер в байтах
};

struct Material {
    explicit Material(resources::ResourceHandle<Shader> shdr_handle)
        : shader_handle(std::move(shdr_handle)) {

        RebuildLayout();
    }

    resources::ResourceHandle<Shader> shader_handle;

    // Единый плоский байтовый массив для ВСЕХ UBO этого материала
    eastl::vector<uint8_t> uniform_buffer;

    // Текстуры материала
    eastl::vector<MaterialTextureBinding> textures;

    // --- КЭШИРОВАННЫЕ ДАННЫЕ (Заполняются 1 раз) ---
    // Готовые инструкции для Рендер-Пасса: кусок памяти -> слот и стадия
    eastl::vector<CachedUBOBinding> cached_ubo_bindings;

    // Быстрая карта параметров: RGTag (hash) -> смещение в uniform_buffer
    eastl::hash_map<RGTag, uint32_t> param_offsets;

    Shader& shader() const { return *shader_handle; }

    // Вызывается в конструкторе или при смене шейдера
    void RebuildLayout() {
        cached_ubo_bindings.clear();
        param_offsets.clear();
        uniform_buffer.clear();

        uint32_t total_size = 0;

        // 1. Собираем карту UBO и смещений параметров
        shader().reflection.ForEachMaterialBinding([this, &total_size](const ShaderReflectedBinding& b) {
            if (b.kind == ShaderResourceKind::UniformBuffer) {
                CachedUBOBinding ubo_info{};
                ubo_info.slot = b.binding;
                ubo_info.stage = b.stage; // Vertex или Fragment
                ubo_info.offset_in_buffer = total_size;
                ubo_info.size = b.size;

                cached_ubo_bindings.push_back(ubo_info);

                // Заминаем офсеты всех параметров внутри этого UBO
                for (const auto& param : b.params) {
                    param_offsets[param.name_hash] = ubo_info.offset_in_buffer + param.offset;
                }

                total_size += b.size;
            }
        });

        // 2. Выделяем ровно столько байт, сколько нужно под все UBO материала
        uniform_buffer.resize(total_size, 0);

        // 3. Копируем дефолтные значения, если они есть
        if (!shader().default_uniform_data.empty()) {
            std::memcpy(uniform_buffer.data(), shader().default_uniform_data.data(),
                        std::min(uniform_buffer.size(), shader().default_uniform_data.size()));
        }
    }

    template <typename T>
    void SetParam(eastl::string_view name, const T& value) {
        SetParamRaw(RGTagOf(name), &value, sizeof(T));
    }

    // Быстрый $O(1)$ вызов SetParam по хэшу
    void SetParamRaw(RGTag tag, const void* data_ptr, uint32_t data_size) {
        TRY_ASSERT(data_ptr, "data_ptr is null");

        auto it = param_offsets.find(tag);
        if (it != param_offsets.end()) {
            uint32_t offset = it->second;
            if (offset + data_size <= uniform_buffer.size()) {
                std::memcpy(uniform_buffer.data() + offset, data_ptr, data_size);
            }
        }
    }

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
        }
    }
};

}  // namespace tryengine::graphics