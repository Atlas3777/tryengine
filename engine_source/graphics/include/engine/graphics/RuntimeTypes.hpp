#pragma once

#include <EASTL/string.h>
#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>
#include <SDL3/SDL_gpu.h>
#include <cstring>

#include "engine/core/Assert.hpp"
#include "engine/resources/ResourceHandle.hpp"
#include "engine/resources/Vertex.hpp"

namespace tryengine::graphics {

using core::LogInfo;

enum class ShaderParamType : uint8_t { Float, Int, Vec2, Vec3, Vec4, Mat3, Mat4 };

constexpr uint32_t GetTypeSize(ShaderParamType type) {
    switch (type) {
        case ShaderParamType::Float:
            return 4;
        case ShaderParamType::Int:
            return 4;
        case ShaderParamType::Vec2:
            return 8;
        case ShaderParamType::Vec3:
            return 16;  // std140 alignment
        case ShaderParamType::Vec4:
            return 16;
        case ShaderParamType::Mat3:
            return 48;  // 3 x vec4
        case ShaderParamType::Mat4:
            return 64;
        default:
            return 0;
    }
}

struct ShaderParamInfo {
    eastl::string name;
    ShaderParamType type;
    uint32_t offset;
    uint32_t size;
};

struct ShaderLayout {
    eastl::vector<ShaderParamInfo> params;
    eastl::unordered_map<eastl::string, uint32_t> texture_slots;
    uint32_t uniform_buffer_size = 0;
    uint32_t uniform_binding_slot = 1;

    const ShaderParamInfo* FindParam(eastl::string_view name) const {
        for (const auto& p : params) {
            if (p.name == name)
                return &p;
        }
        return nullptr;
    }

    int32_t FindTextureSlot(const eastl::string& name) const {
        auto it = texture_slots.find(name);
        return (it != texture_slots.end()) ? static_cast<int32_t>(it->second) : -1;
    }

    void AddParam(const eastl::string& name, ShaderParamType type) {
        uint32_t size = GetTypeSize(type);
        uint32_t alignment = (size > 4) ? 16 : 4;
        uniform_buffer_size = (uniform_buffer_size + alignment - 1) & ~(alignment - 1);
        params.push_back({name, type, uniform_buffer_size, size});
        uniform_buffer_size += size;
    }
};

struct Shader {
    eastl::vector<uint8_t> default_uniform_data;
    SDL_GPUShader* vertex_shader = nullptr;
    SDL_GPUShader* fragment_shader = nullptr;
    ShaderLayout layout;
};

struct TextureSampler {
    SDL_GPUTexture* texture = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    uint32_t slot;
};

struct Mesh {
    SDL_GPUBuffer* vertex_buffer;
    SDL_GPUBuffer* index_buffer;
    uint32_t num_indices;
    resources::VertexFormat v_format = resources::VertexFormat::Standard;
    resources::IndexFormat i_format = resources::IndexFormat::UInt16;
};

struct Material {
    explicit Material(resources::ResourceHandle<Shader> shdr_handle)
        : shader_handle(std::move(shdr_handle)) {
        auto& shdr = *shader_handle;
        uniform_buffer.assign(shdr.layout.uniform_buffer_size, 0);
        if (!shdr.default_uniform_data.empty()) {
            std::memcpy(uniform_buffer.data(), shdr.default_uniform_data.data(),
                        shdr.default_uniform_data.size());
        }
    }

    resources::ResourceHandle<Shader> shader_handle;
    eastl::vector<uint8_t> uniform_buffer;
    eastl::vector<resources::ResourceHandle<TextureSampler>> textures;

    Shader& shader() const { return *shader_handle; }


    void SetTexture(const resources::ResourceHandle<TextureSampler> tex) {
        for (auto& bind : textures) {
            TRY_ASSERT(bind->slot != tex->slot, "ПОПЫТКА ЗАБИНДИТЬ ТЕКСТУРУ В УЖЕ ЗАНЯТЫЙ СЛОТ");
        }
        textures.push_back(tex);
    }

    void SetTexture(const eastl::string& name, const resources::ResourceHandle<TextureSampler> tex) {
        int32_t slot = shader().layout.FindTextureSlot(name);
        // TRY_ASSERT(slot >= 0, "Texture slot witch name {}, not found", name);
        LogInfo("slot {}", slot);
        tex->slot = slot;
        SetTexture(tex);
    }

    template <typename T>
    void SetParam(const eastl::string& name, const T& value) {
        const auto* param = shader().layout.FindParam(name);
        // TRY_ASSERT(param, "НЕ НАЙДЕН PARAM");

        TRY_ASSERT(param->offset + sizeof(T) <= uniform_buffer.size(), "Выход за пределы размера буфера");

        std::memcpy(uniform_buffer.data() + param->offset, &value, sizeof(T));
    }

    void SetParamRaw(const eastl::string& name, const void* data_ptr, uint32_t data_size) {
        TRY_ASSERT(data_ptr, "ПОПЫТКА ВЫСТАВИТЬ PARAM_RAW. data_ptr - nullptr");

        const auto* param = shader().layout.FindParam(name);

        // TRY_ASSERT(param, "НЕ НАЙДЕН PARAM");
        if (!param) {
            // TRY_LOG_ERROR("НЕ НАЙДЕН PARAM");
        }
        else if (param->offset + data_size <= uniform_buffer.size() && data_size <= param->size) {
            std::memcpy(uniform_buffer.data() + param->offset, data_ptr, data_size);
        }
    }
};

}  // namespace tryengine::graphics