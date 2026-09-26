#pragma once

#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <cstdint>
#include <cstring>

#include "engine/core/Result.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/graphics/rg/RGTag.hpp"

namespace tryengine::graphics {

enum class BindingScope : uint32_t {
    Pass = 0,      // set 0: Глобальные ресурсы кадра/прохода
    Material = 1,  // set 1: Параметры материала
    PerObj = 2     // set 2: Ресурсы/Push-константы объекта
};

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
    StorageBufferWrite,
    StorageTexture,
};

// Битовые флаги для стадий шейдера
using ShaderStageFlags = uint32_t;
namespace ShaderStageFlagBits {
    constexpr ShaderStageFlags Vertex   = 1 << 0;
    constexpr ShaderStageFlags Fragment = 1 << 1;
    constexpr ShaderStageFlags Compute  = 1 << 2;
}

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
    ShaderStageFlags stage_flags = 0; // Битовые флаги стадий (VS | FS и т.д.)
    uint32_t set = 0;
    uint32_t binding = 0; // Чистый Vulkan binding (без legacy API slot)
    uint32_t size = 0;
    eastl::vector<ShaderReflectedParam> params;

    [[nodiscard]] BindingScope Scope() const { return static_cast<BindingScope>(set); }
    [[nodiscard]] bool IsReserved() const { return set != static_cast<uint32_t>(BindingScope::Material); }
    [[nodiscard]] bool IsPerObj() const { return set == static_cast<uint32_t>(BindingScope::PerObj); }
    [[nodiscard]] bool IsMaterial() const { return set == static_cast<uint32_t>(BindingScope::Material); }
    [[nodiscard]] bool IsPass() const { return set == static_cast<uint32_t>(BindingScope::Pass); }

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

    [[nodiscard]] bool HasPassBindings() const {
        for (const auto& b : bindings) { if (b.IsPass()) return true; }
        return false;
    }

    [[nodiscard]] bool HasMaterialBindings() const {
        for (const auto& b : bindings) { if (b.IsMaterial()) return true; }
        return false;
    }

    [[nodiscard]] bool HasPerObjBindings() const {
        for (const auto& b : bindings) { if (b.IsPerObj()) return true; }
        return false;
    }

    template <typename Fn>
    void ForEachMaterialBinding(Fn&& fn) const {
        for (const auto& b : bindings) {
            if (b.IsMaterial()) fn(b);
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
        WriteU32(out, b.stage_flags);
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

inline Result<void> ReadReflection(eastl::span<const uint8_t> bytes, size_t& cursor, ShaderReflectionData& out) {
    uint32_t vi_count = 0;
    if (!ReadU32(bytes, cursor, vi_count))
        return LogAndMakeError("unexpected end of buffer reading vertex input count");

    out.vertex_inputs.resize(vi_count);
    for (auto& vi : out.vertex_inputs) {
        if (!ReadString(bytes, cursor, vi.semantic))
            return LogAndMakeError("buffer overflow reading vertex input semantic");
        if (!ReadU32(bytes, cursor, vi.location))
            return LogAndMakeError("buffer overflow reading vertex input location");
    }

    uint32_t binding_count = 0;
    if (!ReadU32(bytes, cursor, binding_count))
        return LogAndMakeError("buffer overflow reading binding count");

    out.bindings.resize(binding_count);
    for (auto& b : out.bindings) {
        if (!ReadString(bytes, cursor, b.name))
            return LogAndMakeError("buffer overflow reading binding name");
        b.name_hash = RGTagOf(b.name);

        uint32_t kind = 0;
        if (!ReadU32(bytes, cursor, kind))
            return LogAndMakeError("buffer overflow reading binding kind");
        if (!ReadU32(bytes, cursor, b.stage_flags))
            return LogAndMakeError("buffer overflow reading binding stage_flags");
        b.kind = static_cast<ShaderResourceKind>(kind);

        if (!ReadU32(bytes, cursor, b.set))
            return LogAndMakeError("buffer overflow reading binding set");
        if (!ReadU32(bytes, cursor, b.binding))
            return LogAndMakeError("buffer overflow reading binding index");
        if (!ReadU32(bytes, cursor, b.size))
            return LogAndMakeError("buffer overflow reading binding size");

        uint32_t param_count = 0;
        if (!ReadU32(bytes, cursor, param_count))
            return LogAndMakeError("buffer overflow reading param count");

        b.params.resize(param_count);
        for (auto& p : b.params) {
            if (!ReadString(bytes, cursor, p.name))
                return LogAndMakeError("buffer overflow reading param name");
            p.name_hash = RGTagOf(p.name);

            uint32_t type = 0;
            if (!ReadU32(bytes, cursor, type))
                return LogAndMakeError("buffer overflow reading param type");
            p.type = static_cast<ShaderParamType>(type);

            if (!ReadU32(bytes, cursor, p.offset))
                return LogAndMakeError("buffer overflow reading param offset");
            if (!ReadU32(bytes, cursor, p.size))
                return LogAndMakeError("buffer overflow reading param size");
        }
    }
    return {};
}

}  // namespace detail

}  // namespace tryengine::graphics