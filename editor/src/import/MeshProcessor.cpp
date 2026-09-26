#include "editor/import/MeshProcessor.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "engine/core/MakeError.hpp"
#include "engine/resources/MeshBinary.hpp"
#include "hlsl++/data_packing.h"

namespace tryeditor {
using namespace tryengine::resources;

namespace {

inline uint32_t PackNormal1010102(const float norm[3]) {
    auto clamp_snorm10 = [](float v) -> int32_t {
        float clamped = std::clamp(v, -1.0f, 1.0f);
        return static_cast<int32_t>(std::round(clamped * 511.0f));
    };
    uint32_t x = static_cast<uint32_t>(clamp_snorm10(norm[0])) & 0x3FF;
    uint32_t y = static_cast<uint32_t>(clamp_snorm10(norm[1])) & 0x3FF;
    uint32_t z = static_cast<uint32_t>(clamp_snorm10(norm[2])) & 0x3FF;
    uint32_t w = 0; // 2 бита под резерв/handedness

    return x | (y << 10) | (z << 20) | (w << 30);
}

inline void PackNormalByte4SNorm(const float norm[3], int8_t out_norm[4]) {
    auto clamp_snorm8 = [](float v) -> int8_t {
        float clamped = std::clamp(v, -1.0f, 1.0f);
        return static_cast<int8_t>(std::round(clamped * 127.0f));
    };
    out_norm[0] = clamp_snorm8(norm[0]);
    out_norm[1] = clamp_snorm8(norm[1]);
    out_norm[2] = clamp_snorm8(norm[2]);
    out_norm[3] = 0; // Padding / Handedness
}

inline void PackUVHalf2(const float uv[2], uint16_t out_uv[2]) {
    out_uv[0] = hlslpp::f32tof16(uv[0]);
    out_uv[1] = hlslpp::f32tof16(uv[1]);
}

inline uint8_t PackUnorm8(float val) {
    float clamped = std::clamp(val, 0.0f, 1.0f);
    return static_cast<uint8_t>(std::round(clamped * 255.0f));
}

void ReadJoints4(const uint8_t* ptr, uint32_t component_type, uint32_t stride, uint32_t out_joints[4]) {
    if (!ptr) {
        out_joints[0] = out_joints[1] = out_joints[2] = out_joints[3] = 0;
        return;
    }
    if (component_type == 5123) { // UNSIGNED_SHORT
        const uint16_t* u16 = reinterpret_cast<const uint16_t*>(ptr);
        out_joints[0] = u16[0]; out_joints[1] = u16[1];
        out_joints[2] = u16[2]; out_joints[3] = u16[3];
    } else { // UNSIGNED_BYTE (5121)
        out_joints[0] = ptr[0]; out_joints[1] = ptr[1];
        out_joints[2] = ptr[2]; out_joints[3] = ptr[3];
    }
}

void ReadWeights4(const uint8_t* ptr, uint32_t component_type, uint32_t stride, float out_weights[4]) {
    if (!ptr) {
        out_weights[0] = 1.0f; out_weights[1] = 0.0f;
        out_weights[2] = 0.0f; out_weights[3] = 0.0f;
        return;
    }
    if (component_type == 5126) { // FLOAT
        const float* f = reinterpret_cast<const float*>(ptr);
        out_weights[0] = f[0]; out_weights[1] = f[1];
        out_weights[2] = f[2]; out_weights[3] = f[3];
    } else if (component_type == 5121) { // UNSIGNED_BYTE
        out_weights[0] = ptr[0] / 255.0f; out_weights[1] = ptr[1] / 255.0f;
        out_weights[2] = ptr[2] / 255.0f; out_weights[3] = ptr[3] / 255.0f;
    } else if (component_type == 5123) { // UNSIGNED_SHORT
        const uint16_t* u16 = reinterpret_cast<const uint16_t*>(ptr);
        out_weights[0] = u16[0] / 65535.0f; out_weights[1] = u16[1] / 65535.0f;
        out_weights[2] = u16[2] / 65535.0f; out_weights[3] = u16[3] / 65535.0f;
    }
}

eastl::string AttributeFlagsToString(uint32_t attr_flags) {
    eastl::string result = "Position"; // Позиция присутствуют всегда

    if (attr_flags & static_cast<uint32_t>(MeshAttributeFlags::Normal))    result += ", Normal";
    if (attr_flags & static_cast<uint32_t>(MeshAttributeFlags::Tangent))   result += ", Tangent";
    if (attr_flags & static_cast<uint32_t>(MeshAttributeFlags::TexCoord0)) result += ", TexCoord0";
    if (attr_flags & static_cast<uint32_t>(MeshAttributeFlags::TexCoord1)) result += ", TexCoord1";
    if (attr_flags & static_cast<uint32_t>(MeshAttributeFlags::Color0))    result += ", Color0";
    if (attr_flags & static_cast<uint32_t>(MeshAttributeFlags::Joints0))   result += ", Joints0";
    if (attr_flags & static_cast<uint32_t>(MeshAttributeFlags::Weights0))  result += ", Weights0";

    return result;
}

} // namespace

tryengine::Result<eastl::vector<uint8_t>> MeshProcessor::ProcessPrimitive(
    const RawPrimitiveInput& input,
    const MeshProcessSettings& settings) {

    if (!input.positions.data || input.positions.count == 0) {
        return LogAndMakeError("MeshProcessor: Position stream is missing or empty");
    }

    const uint32_t v_count = input.positions.count;

    // 1. Формирование битовой маски атрибутов
    uint32_t attr_flags = static_cast<uint32_t>(MeshAttributeFlags::None);
    if (input.normals.data) attr_flags |= static_cast<uint32_t>(MeshAttributeFlags::Normal);
    if (input.uvs.data)     attr_flags |= static_cast<uint32_t>(MeshAttributeFlags::TexCoord0);
    if (input.colors.data)  attr_flags |= static_cast<uint32_t>(MeshAttributeFlags::Color0);
    if (input.joints.data)  attr_flags |= static_cast<uint32_t>(MeshAttributeFlags::Joints0);
    if (input.weights.data) attr_flags |= static_cast<uint32_t>(MeshAttributeFlags::Weights0);

    const uint32_t attr_stride = CalculateAttributeBufferStride(static_cast<MeshAttributeFlags>(attr_flags));
    eastl::vector<uint8_t> position_buffer(v_count * sizeof(float) * 3);
    eastl::vector<uint8_t> attribute_buffer(v_count * attr_stride);

    float min_bounds[3] = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    float max_bounds[3] = { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };

    for (uint32_t v = 0; v < v_count; ++v) {
        // --- Позиции ---
        const float* pos = reinterpret_cast<const float*>(input.positions.data + (v * input.positions.stride));
        std::memcpy(position_buffer.data() + (v * GetPositionStride()), pos, GetPositionStride());

        if (settings.generate_aabb || settings.generate_radius) {
            for (int i = 0; i < 3; ++i) {
                min_bounds[i] = std::min(min_bounds[i], pos[i]);
                max_bounds[i] = std::max(max_bounds[i], pos[i]);
            }
        }

        // --- Интерливинг атрибутов ---
        uint8_t* dst = attribute_buffer.data() + (v * attr_stride);

        if (attr_flags & static_cast<uint32_t>(MeshAttributeFlags::Normal)) {
            const float* norm = reinterpret_cast<const float*>(input.normals.data + (v * input.normals.stride));
            PackNormalByte4SNorm(norm, reinterpret_cast<int8_t*>(dst));
            dst += 4;
        }

        if (attr_flags & static_cast<uint32_t>(MeshAttributeFlags::TexCoord0)) {
            const float* uv = reinterpret_cast<const float*>(input.uvs.data + (v * input.uvs.stride));
            PackUVHalf2(uv, reinterpret_cast<uint16_t*>(dst));
            dst += 4;
        }

        if (attr_flags & static_cast<uint32_t>(MeshAttributeFlags::Color0)) {
            const float* col = reinterpret_cast<const float*>(input.colors.data + (v * input.colors.stride));
            float color_rgba[4] = { col[0], col[1], col[2], (input.color_components == 4) ? col[3] : 1.0f };
            dst[0] = PackUnorm8(color_rgba[0]);
            dst[1] = PackUnorm8(color_rgba[1]);
            dst[2] = PackUnorm8(color_rgba[2]);
            dst[3] = PackUnorm8(color_rgba[3]);
            dst += 4;
        }

        if (attr_flags & static_cast<uint32_t>(MeshAttributeFlags::Joints0)) {
            uint32_t joints[4];
            const uint8_t* j_ptr = input.joints.data + (v * input.joints.stride);
            ReadJoints4(j_ptr, input.joint_component_type, input.joints.stride, joints);
            dst[0] = static_cast<uint8_t>(joints[0] & 0xFF);
            dst[1] = static_cast<uint8_t>(joints[1] & 0xFF);
            dst[2] = static_cast<uint8_t>(joints[2] & 0xFF);
            dst[3] = static_cast<uint8_t>(joints[3] & 0xFF);
            dst += 4;
        }

        if (attr_flags & static_cast<uint32_t>(MeshAttributeFlags::Weights0)) {
            float weights[4];
            const uint8_t* w_ptr = input.weights.data + (v * input.weights.stride);
            ReadWeights4(w_ptr, input.weight_component_type, input.weights.stride, weights);
            dst[0] = PackUnorm8(weights[0]);
            dst[1] = PackUnorm8(weights[1]);
            dst[2] = PackUnorm8(weights[2]);
            dst[3] = PackUnorm8(weights[3]);
            dst += 4;
        }
    }

    // --- Расчет радиуса bounding sphere ---
    float radius = 0.0f;
    if (settings.generate_radius) {
        const float center[3] = {
            (min_bounds[0] + max_bounds[0]) * 0.5f,
            (min_bounds[1] + max_bounds[1]) * 0.5f,
            (min_bounds[2] + max_bounds[2]) * 0.5f
        };

        float max_dist_sq = 0.0f;
        for (uint32_t v = 0; v < v_count; ++v) {
            const float* pos = reinterpret_cast<const float*>(position_buffer.data() + (v * sizeof(float) * 3));
            float dx = pos[0] - center[0];
            float dy = pos[1] - center[1];
            float dz = pos[2] - center[2];
            float dist_sq = dx * dx + dy * dy + dz * dz;
            if (dist_sq > max_dist_sq) {
                max_dist_sq = dist_sq;
            }
        }
        radius = std::sqrt(max_dist_sq);
    }

    // --- Индексы ---
    const uint32_t index_count = (input.indices.data && input.indices.format != IndexFormat::None) ? input.indices.count : v_count;

    // Автоподбор формата индексов с поддержкой UInt8 (< 256 вершин)
    const IndexFormat target_index_format = (v_count <= 255)   ? IndexFormat::UInt8
                                          : (v_count <= 65535) ? IndexFormat::UInt16
                                                               : IndexFormat::UInt32;
    const uint32_t index_stride = GetIndexStride(target_index_format);

    eastl::vector<uint8_t> index_buffer(index_count * index_stride);

    if (input.indices.data && input.indices.format != IndexFormat::None) {
        for (uint32_t id = 0; id < index_count; ++id) {
            const uint8_t* raw_idx = input.indices.data + (id * input.indices.stride);
            uint32_t val = 0;
            if (input.indices.format == IndexFormat::UInt16) {
                val = *reinterpret_cast<const uint16_t*>(raw_idx);
            } else if (input.indices.format == IndexFormat::UInt32) {
                val = *reinterpret_cast<const uint32_t*>(raw_idx);
            } else { // IndexFormat::UInt8
                val = *raw_idx;
            }

            if (target_index_format == IndexFormat::UInt8) {
                index_buffer[id] = static_cast<uint8_t>(val);
            } else if (target_index_format == IndexFormat::UInt16) {
                reinterpret_cast<uint16_t*>(index_buffer.data())[id] = static_cast<uint16_t>(val);
            } else {
                reinterpret_cast<uint32_t*>(index_buffer.data())[id] = val;
            }
        }
    } else {
        if (target_index_format == IndexFormat::UInt8) {
            for (uint32_t id = 0; id < index_count; ++id) index_buffer[id] = static_cast<uint8_t>(id);
        } else if (target_index_format == IndexFormat::UInt16) {
            auto* dst = reinterpret_cast<uint16_t*>(index_buffer.data());
            for (uint32_t id = 0; id < index_count; ++id) dst[id] = static_cast<uint16_t>(id);
        } else {
            auto* dst = reinterpret_cast<uint32_t*>(index_buffer.data());
            for (uint32_t id = 0; id < index_count; ++id) dst[id] = id;
        }
    }

    eastl::string attr_str = AttributeFlagsToString(attr_flags);
    LogInfo("Mesh primitive processed successfully: {} vertices, {} indices, attributes: [{}]",
            v_count, index_count, attr_str.c_str());

    MeshHeader header;
    header.attribute_flags = static_cast<MeshAttributeFlags>(attr_flags);
    header.index_format = target_index_format;
    header.radius = radius;
    if (settings.generate_aabb) {
        std::memcpy(header.bbox_min, min_bounds, sizeof(min_bounds));
        std::memcpy(header.bbox_max, max_bounds, sizeof(max_bounds));
    }

    return MeshBinary::Pack(header, position_buffer, attribute_buffer, index_buffer);
}

} // namespace tryeditor