#pragma once
#include <cstring>

#include "engine/graphics/Types.hpp"

namespace tryengine::graphics {

enum class ShaderParamType : uint8_t { Float, Int, Vec2, Vec3, Vec4, Mat3, Mat4 };

constexpr uint32_t GetTypeSize(ShaderParamType type) {
    switch (type) {
        case ShaderParamType::Float:return 4;
        case ShaderParamType::Int:return 4;
        case ShaderParamType::Vec2:return 8;
        case ShaderParamType::Vec3:return 16;  // std140 alignment
        case ShaderParamType::Vec4:return 16;
        case ShaderParamType::Mat3:return 48;  // 3 x vec4
        case ShaderParamType::Mat4:return 64;
        default:return 0;
    }
}

struct ShaderAssetParam {
    std::string name;
    ShaderParamType type;
    std::vector<float> default_values;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("name", name), cereal::make_nvp("type", type),
           cereal::make_nvp("defaults", default_values));
    }
};

struct ShaderAssetTexture {
    std::string name;
    uint32_t slot;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("name", name), cereal::make_nvp("slot", slot));
    }
};

struct ShaderAsset {
    uint64_t vertex_shader_id = 0;
    uint64_t fragment_shader_id = 0;
    std::vector<ShaderAssetParam> params;
    std::vector<ShaderAssetTexture> textures;

    template <class Archive>
    void serialize(Archive& ar) {
        ar(cereal::make_nvp("vertex_id", vertex_shader_id), cereal::make_nvp("fragment_id", fragment_shader_id),
           cereal::make_nvp("params", params), cereal::make_nvp("textures", textures));
    }
};

struct ShaderParamInfo {
    std::string name;
    ShaderParamType type;
    uint32_t offset;
    uint32_t size;
};

struct ShaderLayout {
    std::vector<ShaderParamInfo> params;
    std::unordered_map<std::string, uint32_t> texture_slots;
    uint32_t uniform_buffer_size = 0;
    uint32_t uniform_binding_slot = 1;

    const ShaderParamInfo* FindParam(const std::string& name) const {
        for (const auto& p : params) {
            if (p.name == name)
                return &p;
        }
        return nullptr;
    }

    int32_t FindTextureSlot(const std::string& name) const {
        auto it = texture_slots.find(name);
        return (it != texture_slots.end()) ? static_cast<int32_t>(it->second) : -1;
    }

    void AddParam(const std::string& name, ShaderParamType type) {
        uint32_t size = GetTypeSize(type);
        uint32_t alignment = (size > 4) ? 16 : 4;
        uniform_buffer_size = (uniform_buffer_size + alignment - 1) & ~(alignment - 1);
        params.push_back({name, type, uniform_buffer_size, size});
        uniform_buffer_size += size;
    }
};

struct Shader {
    SDL_GPUShader* vertex_shader = nullptr;
    SDL_GPUShader* fragment_shader = nullptr;
    ShaderLayout layout;
    std::vector<uint8_t> default_uniform_data;
};

struct Material {
    Material() = default;
    explicit Material(Shader* shdr) { Attach(shdr); }

    Shader* shader = nullptr;
    std::vector<uint8_t> uniform_buffer;

    std::unordered_map<uint32_t, Texture> textures;

    void Attach(Shader* shdr) {
        if (!shdr)
            return;
        shader = shdr;

        uniform_buffer.assign(shader->layout.uniform_buffer_size, 0);

        if (!shader->default_uniform_data.empty()) {
            std::memcpy(uniform_buffer.data(), shader->default_uniform_data.data(),
                        shader->default_uniform_data.size());
        }
    }

    // Установка текстуры по слоту
    void SetTexture(uint32_t slot, const Texture& tex) { textures[slot] = tex; }

    // Установка текстуры по имени
    void SetTexture(const std::string& name, const Texture& tex) {
        if (!shader)
            return;
        int32_t slot = shader->layout.FindTextureSlot(name);
        if (slot >= 0) {
            SetTexture(static_cast<uint32_t>(slot), tex);
        }
    }

    template <typename T>
    void SetParam(const std::string& name, const T& value) {
        if (!shader)
            return;
        const auto* param = shader->layout.FindParam(name);
        if (!param)
            return;

        if (param->offset + sizeof(T) <= uniform_buffer.size()) {
            std::memcpy(uniform_buffer.data() + param->offset, &value, sizeof(T));
        }
    }

    void SetParamRaw(const std::string& name, const void* data_ptr, uint32_t data_size) {
        if (!shader)
            return;
        const auto* param = shader->layout.FindParam(name);
        if (!param || !data_ptr)
            return;

        if (param->offset + data_size <= uniform_buffer.size() && data_size <= param->size) {
            std::memcpy(uniform_buffer.data() + param->offset, data_ptr, data_size);
        }
    }

    uint8_t* GetUniformDataPtr() { return uniform_buffer.data(); }
    const uint8_t* GetUniformDataPtr() const { return uniform_buffer.data(); }
};

}  // namespace tryengine::graphics