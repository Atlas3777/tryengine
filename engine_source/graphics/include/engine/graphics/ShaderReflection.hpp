#pragma once

#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <cstdint>
#include <cstring>

#include "engine/graphics/rg/RGTag.hpp"

namespace tryengine::graphics {

namespace reserved_names {
constexpr const char* kFrameUBO = "FrameUBO";
constexpr const char* kGlobalLightUBO = "GlobalLightUBO";
constexpr const char* kPointLightBuffer = "PointLightBuffer";
}  // namespace reserved_names

enum class ShaderParamType : uint8_t { Float, Int, Vec2, Vec3, Vec4, Mat3, Mat4 };

constexpr uint32_t GetTypeSize(ShaderParamType type) {
    switch (type) {
        case ShaderParamType::Float: return 4;
        case ShaderParamType::Int:   return 4;
        case ShaderParamType::Vec2:  return 8;
        case ShaderParamType::Vec3:  return 12;
        case ShaderParamType::Vec4:  return 16;
        case ShaderParamType::Mat3:  return 36;
        case ShaderParamType::Mat4:  return 64;
    }
    return 0;
}

enum class ShaderResourceKind : uint8_t {
    UniformBuffer,
    SampledTexture,
    StorageBufferRead,
};

enum class ShaderStage : uint8_t { Vertex, Fragment };

struct ShaderReflectedParam {
    eastl::string name;
    RGTag name_hash = 0;
    ShaderParamType type = ShaderParamType::Float;
    uint32_t offset = 0;
    uint32_t size = 0;
};

struct ShaderReflectedBinding {
    eastl::string name;
    RGTag name_hash = 0;
    ShaderResourceKind kind = ShaderResourceKind::UniformBuffer;
    ShaderStage stage = ShaderStage::Fragment;
    uint32_t set = 0;
    uint32_t binding = 0;
    uint32_t size = 0;
    eastl::vector<ShaderReflectedParam> params;

    [[nodiscard]] bool IsReserved() const {
        return name == reserved_names::kFrameUBO || name == reserved_names::kGlobalLightUBO ||
               name == reserved_names::kPointLightBuffer;
    }

    [[nodiscard]] const ShaderReflectedParam* FindParamByTag(RGTag tag) const {
        for (const auto& p : params) {
            if (p.name_hash == tag) return &p;
        }
        return nullptr;
    }

    [[nodiscard]] const ShaderReflectedParam* FindParam(eastl::string_view param_name) const {
        return FindParamByTag(RGTagOf(param_name));
    }
};

struct ShaderReflectedVertexInput {
    eastl::string semantic;
    uint32_t location = 0;
};

struct ShaderReflectionData {
    eastl::vector<ShaderReflectedVertexInput> vertex_inputs;
    eastl::vector<ShaderReflectedBinding> bindings;

    [[nodiscard]] const ShaderReflectedBinding* FindBindingByTag(RGTag tag) const {
        for (const auto& b : bindings) {
            if (b.name_hash == tag) return &b;
        }
        return nullptr;
    }

    [[nodiscard]] const ShaderReflectedBinding* FindBinding(eastl::string_view name) const {
        return FindBindingByTag(RGTagOf(name));
    }

    template <typename Fn>
    void ForEachMaterialBinding(Fn&& fn) const {
        for (const auto& b : bindings) {
            if (!b.IsReserved()) fn(b);
        }
    }
};

namespace detail {

inline void WriteU32(eastl::vector<uint8_t>& out, uint32_t v) {
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    out.insert(out.end(), p, p + sizeof(uint32_t));
}

inline void WriteString(eastl::vector<uint8_t>& out, eastl::string_view s) {
    WriteU32(out, static_cast<uint32_t>(s.size()));
    out.insert(out.end(), reinterpret_cast<const uint8_t*>(s.data()),
               reinterpret_cast<const uint8_t*>(s.data()) + s.size());
}

inline bool ReadU32(eastl::span<const uint8_t> bytes, size_t& cursor, uint32_t& out) {
    if (cursor + sizeof(uint32_t) > bytes.size()) return false;
    std::memcpy(&out, bytes.data() + cursor, sizeof(uint32_t));
    cursor += sizeof(uint32_t);
    return true;
}

inline bool ReadString(eastl::span<const uint8_t> bytes, size_t& cursor, eastl::string& out) {
    uint32_t len = 0;
    if (!ReadU32(bytes, cursor, len)) return false;
    if (cursor + len > bytes.size()) return false;
    out.assign(reinterpret_cast<const char*>(bytes.data() + cursor), len);
    cursor += len;
    return true;
}

inline void WriteReflection(eastl::vector<uint8_t>& out, const ShaderReflectionData& refl) {
    WriteU32(out, static_cast<uint32_t>(refl.vertex_inputs.size()));
    for (const auto& vi : refl.vertex_inputs) {
        WriteString(out, vi.semantic);
        WriteU32(out, vi.location);
    }

    WriteU32(out, static_cast<uint32_t>(refl.bindings.size()));
    for (const auto& b : refl.bindings) {
        WriteString(out, b.name);
        WriteU32(out, static_cast<uint32_t>(b.kind));
        WriteU32(out, static_cast<uint32_t>(b.stage));
        WriteU32(out, b.set);
        WriteU32(out, b.binding);
        WriteU32(out, b.size);

        WriteU32(out, static_cast<uint32_t>(b.params.size()));
        for (const auto& p : b.params) {
            WriteString(out, p.name);
            WriteU32(out, static_cast<uint32_t>(p.type));
            WriteU32(out, p.offset);
            WriteU32(out, p.size);
        }
    }
}

inline bool ReadReflection(eastl::span<const uint8_t> bytes, size_t& cursor, ShaderReflectionData& out) {
    uint32_t vi_count = 0;
    if (!ReadU32(bytes, cursor, vi_count)) return false;
    out.vertex_inputs.resize(vi_count);
    for (auto& vi : out.vertex_inputs) {
        if (!ReadString(bytes, cursor, vi.semantic)) return false;
        if (!ReadU32(bytes, cursor, vi.location)) return false;
    }

    uint32_t binding_count = 0;
    if (!ReadU32(bytes, cursor, binding_count)) return false;
    out.bindings.resize(binding_count);
    for (auto& b : out.bindings) {
        if (!ReadString(bytes, cursor, b.name)) return false;
        b.name_hash = RGTagOf(b.name);

        uint32_t kind = 0, stage = 0;
        if (!ReadU32(bytes, cursor, kind)) return false;
        if (!ReadU32(bytes, cursor, stage)) return false;
        b.kind = static_cast<ShaderResourceKind>(kind);
        b.stage = static_cast<ShaderStage>(stage);

        if (!ReadU32(bytes, cursor, b.set)) return false;
        if (!ReadU32(bytes, cursor, b.binding)) return false;
        if (!ReadU32(bytes, cursor, b.size)) return false;

        uint32_t param_count = 0;
        if (!ReadU32(bytes, cursor, param_count)) return false;
        b.params.resize(param_count);
        for (auto& p : b.params) {
            if (!ReadString(bytes, cursor, p.name)) return false;
            p.name_hash = RGTagOf(p.name);
            uint32_t type = 0;
            if (!ReadU32(bytes, cursor, type)) return false;
            p.type = static_cast<ShaderParamType>(type);
            if (!ReadU32(bytes, cursor, p.offset)) return false;
            if (!ReadU32(bytes, cursor, p.size)) return false;
        }
    }
    return true;
}

}  // namespace detail

}  // namespace tryengine::graphics