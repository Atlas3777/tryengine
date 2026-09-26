#pragma once

#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>
#include <cstdint>
#include <cstring>

#include "engine/core/MakeError.hpp"
#include "engine/core/Result.hpp"
#include "engine/graphics/ShaderReflection.hpp"

namespace tryengine::graphics {

constexpr uint32_t kShaderBinaryMagic = 0x44485354u;  // 'TSHD'
constexpr uint32_t kShaderBinaryVersion = 1u;

struct StageResourceCounts {
    uint32_t num_samplers = 0;
    uint32_t num_storage_buffers = 0;
    uint32_t num_storage_textures = 0;
    uint32_t num_uniform_buffers = 0;
};

#pragma pack(push, 1)
struct ShaderContainerHeader {
    uint32_t magic = kShaderBinaryMagic;
    uint32_t version = kShaderBinaryVersion;

    StageResourceCounts vertex_counts;
    StageResourceCounts fragment_counts;

    uint32_t offset_reflect_data = 0;
    uint32_t size_reflect_data = 0;

    uint32_t offset_vertex_spv = 0;
    uint32_t size_vertex_spv = 0;

    uint32_t offset_fragment_spv = 0;
    uint32_t size_fragment_spv = 0;
};
#pragma pack(pop)

static_assert(sizeof(ShaderContainerHeader) == 64);

struct ShaderBinaryContent {
    StageResourceCounts vertex_counts;
    StageResourceCounts fragment_counts;
    ShaderReflectionData reflection;
    eastl::vector<uint8_t> vertex_spv;
    eastl::vector<uint8_t> fragment_spv;
};

inline eastl::vector<uint8_t> PackShaderBinary(const ShaderBinaryContent& content) {
    eastl::vector<uint8_t> reflection_bytes;
    detail::WriteReflection(reflection_bytes, content.reflection);

    ShaderContainerHeader header{};
    header.magic = kShaderBinaryMagic;
    header.version = kShaderBinaryVersion;
    header.vertex_counts = content.vertex_counts;
    header.fragment_counts = content.fragment_counts;

    uint32_t current_offset = sizeof(ShaderContainerHeader);

    header.offset_reflect_data = current_offset;
    header.size_reflect_data = static_cast<uint32_t>(reflection_bytes.size());
    current_offset += header.size_reflect_data;

    header.offset_vertex_spv = current_offset;
    header.size_vertex_spv = static_cast<uint32_t>(content.vertex_spv.size());
    current_offset += header.size_vertex_spv;

    header.offset_fragment_spv = current_offset;
    header.size_fragment_spv = static_cast<uint32_t>(content.fragment_spv.size());
    current_offset += header.size_fragment_spv;

    eastl::vector<uint8_t> result;
    result.resize(current_offset);

    std::memcpy(result.data(), &header, sizeof(ShaderContainerHeader));

    if (header.size_reflect_data > 0) {
        std::memcpy(result.data() + header.offset_reflect_data, reflection_bytes.data(), header.size_reflect_data);
    }
    if (header.size_vertex_spv > 0) {
        std::memcpy(result.data() + header.offset_vertex_spv, content.vertex_spv.data(), header.size_vertex_spv);
    }
    if (header.size_fragment_spv > 0) {
        std::memcpy(result.data() + header.offset_fragment_spv, content.fragment_spv.data(), header.size_fragment_spv);
    }

    return result;
}

inline bool SafeCheckBounds(size_t offset, size_t size, size_t total_size) {
    return (offset <= total_size) && (size <= total_size - offset);
}

inline Result<ShaderBinaryContent> UnpackShaderBinary(eastl::span<const uint8_t> bytes) {
    if (bytes.size() < sizeof(ShaderContainerHeader))
        return LogAndMakeError("Shader file header size mismatch");

    ShaderBinaryContent out_content;
    ShaderContainerHeader header{};
    std::memcpy(&header, bytes.data(), sizeof(ShaderContainerHeader));

    if (header.magic != kShaderBinaryMagic || header.version != kShaderBinaryVersion) {
        return LogAndMakeError("Invalid shader binary magic or version");
    }

    if (!SafeCheckBounds(header.offset_reflect_data, header.size_reflect_data, bytes.size()) ||
        !SafeCheckBounds(header.offset_vertex_spv, header.size_vertex_spv, bytes.size()) ||
        !SafeCheckBounds(header.offset_fragment_spv, header.size_fragment_spv, bytes.size())) {
        return LogAndMakeError("Corrupted shader binary: offset/size out of bounds");
    }

    out_content.vertex_counts = header.vertex_counts;
    out_content.fragment_counts = header.fragment_counts;

    if (header.size_reflect_data > 0) {
        const eastl::span refl_span(bytes.data() + header.offset_reflect_data, header.size_reflect_data);
        size_t cursor = 0;
        auto res = detail::ReadReflection(refl_span, cursor, out_content.reflection);
        if (!res.has_value()){
            return LogAndMakeError("Failed to deserialize shader reflection: {}", res.error());
        }
    }

    if (header.size_vertex_spv > 0) {
        out_content.vertex_spv.assign(bytes.data() + header.offset_vertex_spv,
                                      bytes.data() + header.offset_vertex_spv + header.size_vertex_spv);
    }

    if (header.size_fragment_spv > 0) {
        out_content.fragment_spv.assign(bytes.data() + header.offset_fragment_spv,
                                        bytes.data() + header.offset_fragment_spv + header.size_fragment_spv);
    }

    return out_content;
}

}  // namespace tryengine::graphics