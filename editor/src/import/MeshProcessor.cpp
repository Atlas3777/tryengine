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
    out_norm[3] = 0; // Padding / Handedness (если понадобится в будущем)
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

} // namespace

tryengine::Result<eastl::vector<uint8_t>> MeshProcessor::ProcessPrimitive(
    const RawPrimitiveInput& input,
    const MeshProcessSettings& settings) {

    if (!input.positions.data || input.positions.count == 0) {
        return LogAndMakeError("MeshProcessor: Position stream is missing or empty");
    }

    const uint32_t v_count = input.positions.count;

    // Определяем формат вершин
    VertexFormat format = settings.target_format;
    if (settings.auto_select_format) {
        if (input.joints.data && input.weights.data) {
            format = VertexFormat::SkinnedPacked;
        } else if (input.normals.data || input.uvs.data || input.colors.data) {
            format = VertexFormat::StaticPacked;
        } else {
            format = VertexFormat::PositionOnly;
        }
    }

    const uint32_t stride = GetVertexStride(format);
    eastl::vector<uint8_t> vertex_buffer(v_count * stride);
    uint8_t* write_ptr = vertex_buffer.data();

    float min_bounds[3] = { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() };
    float max_bounds[3] = { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() };

    // Заполнение вертексного буфера в зависимости от целевого формата
    for (uint32_t v = 0; v < v_count; ++v) {
        const float* pos = reinterpret_cast<const float*>(input.positions.data + (v * input.positions.stride));

        if (settings.generate_aabb) {
            for (int i = 0; i < 3; ++i) {
                if (pos[i] < min_bounds[i]) min_bounds[i] = pos[i];
                if (pos[i] > max_bounds[i]) max_bounds[i] = pos[i];
            }
        }

        const float* norm = input.normals.data ? reinterpret_cast<const float*>(input.normals.data + (v * input.normals.stride)) : nullptr;
        const float* uv   = input.uvs.data     ? reinterpret_cast<const float*>(input.uvs.data + (v * input.uvs.stride)) : nullptr;
        const float* col  = input.colors.data  ? reinterpret_cast<const float*>(input.colors.data + (v * input.colors.stride)) : nullptr;

        float dummy_norm[3] = { 0.0f, 1.0f, 0.0f };
        float dummy_uv[2]   = { 0.0f, 0.0f };
        float dummy_col[4]  = { 1.0f, 1.0f, 1.0f, 1.0f };

        const float* norm_ptr = norm ? norm : dummy_norm;
        const float* uv_ptr   = uv   ? uv   : dummy_uv;

        float color_rgba[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        if (col) {
            color_rgba[0] = col[0]; color_rgba[1] = col[1]; color_rgba[2] = col[2];
            color_rgba[3] = (input.color_components == 4) ? col[3] : 1.0f;
        }

        switch (format) {
            case VertexFormat::None: {
                TRY_ASSERT(false, "VertexFormat::None");
                break;
            }
            case VertexFormat::StaticPacked: {
                auto* dst = reinterpret_cast<VertexStaticPacked*>(write_ptr);
                std::memcpy(dst->position, pos, sizeof(float) * 3);

                // Вместо dst->normal = PackNormal1010102(norm_ptr);
                PackNormalByte4SNorm(norm_ptr, dst->normal);

                dst->color[0] = PackUnorm8(color_rgba[0]);
                dst->color[1] = PackUnorm8(color_rgba[1]);
                dst->color[2] = PackUnorm8(color_rgba[2]);
                dst->color[3] = PackUnorm8(color_rgba[3]);
                PackUVHalf2(uv_ptr, dst->uv);
                std::memset(dst->padding, 0, sizeof(dst->padding));
                break;
            }
            case VertexFormat::SkinnedPacked: {
                auto* dst = reinterpret_cast<VertexSkinnedPacked*>(write_ptr);
                std::memcpy(dst->position, pos, sizeof(float) * 3);
                dst->normal = PackNormal1010102(norm_ptr);
                dst->color[0] = PackUnorm8(color_rgba[0]);
                dst->color[1] = PackUnorm8(color_rgba[1]);
                dst->color[2] = PackUnorm8(color_rgba[2]);
                dst->color[3] = PackUnorm8(color_rgba[3]);
                PackUVHalf2(uv_ptr, dst->uv);

                uint32_t joints[4];
                float weights[4];
                const uint8_t* j_ptr = input.joints.data  ? input.joints.data + (v * input.joints.stride)   : nullptr;
                const uint8_t* w_ptr = input.weights.data ? input.weights.data + (v * input.weights.stride) : nullptr;

                ReadJoints4(j_ptr, input.joint_component_type, input.joints.stride, joints);
                ReadWeights4(w_ptr, input.weight_component_type, input.weights.stride, weights);

                for (int i = 0; i < 4; ++i) {
                    dst->boneIndices[i] = static_cast<uint8_t>(joints[i] & 0xFF);
                    dst->boneWeights[i] = PackUnorm8(weights[i]);
                }
                break;
            }
            // case VertexFormat::Standard: {
            //     auto* dst = reinterpret_cast<Vertex*>(write_ptr);
            //     dst->x = pos[0]; dst->y = pos[1]; dst->z = pos[2];
            //     dst->nx = norm_ptr[0]; dst->ny = norm_ptr[1]; dst->nz = norm_ptr[2];
            //     dst->r = color_rgba[0]; dst->g = color_rgba[1]; dst->b = color_rgba[2]; dst->a = color_rgba[3];
            //     dst->u = uv_ptr[0]; dst->v = uv_ptr[1];
            //     break;
            // }
            case VertexFormat::SkinnedStandard: {
                auto* dst = reinterpret_cast<VertexSkinned*>(write_ptr);
                dst->x = pos[0]; dst->y = pos[1]; dst->z = pos[2];
                dst->nx = norm_ptr[0]; dst->ny = norm_ptr[1]; dst->nz = norm_ptr[2];
                dst->r = color_rgba[0]; dst->g = color_rgba[1]; dst->b = color_rgba[2]; dst->a = color_rgba[3];
                dst->u = uv_ptr[0]; dst->v = uv_ptr[1];

                const uint8_t* j_ptr = input.joints.data  ? input.joints.data + (v * input.joints.stride)   : nullptr;
                const uint8_t* w_ptr = input.weights.data ? input.weights.data + (v * input.weights.stride) : nullptr;

                ReadJoints4(j_ptr, input.joint_component_type, input.joints.stride, dst->boneIndices);
                ReadWeights4(w_ptr, input.weight_component_type, input.weights.stride, dst->boneWeights);
                break;
            }
            case VertexFormat::PositionOnly: {
                auto* dst = reinterpret_cast<VertexPositionOnly*>(write_ptr);
                dst->x = pos[0]; dst->y = pos[1]; dst->z = pos[2];
                break;
            }
            case VertexFormat::Ui2D: {
                auto* dst = reinterpret_cast<Vertex2D*>(write_ptr);
                dst->x = pos[0]; dst->y = pos[1];
                dst->u = uv_ptr[0]; dst->v = uv_ptr[1];
                dst->r = PackUnorm8(color_rgba[0]); dst->g = PackUnorm8(color_rgba[1]);
                dst->b = PackUnorm8(color_rgba[2]); dst->a = PackUnorm8(color_rgba[3]);
                break;
            }
        }

        write_ptr += stride;
    }

    // --- Подготовка заголовка ---
    MeshHeader header;
    header.vertex_format = format;
    if (settings.generate_aabb) {
        std::memcpy(header.bbox_min, min_bounds, sizeof(min_bounds));
        std::memcpy(header.bbox_max, max_bounds, sizeof(max_bounds));
    }

    // --- Выбор формата индексов и упаковка ---
    const uint32_t index_count = (input.indices.data && input.indices.format != IndexFormat::None)
                                      ? input.indices.count
                                      : v_count;

    // Выбираем UInt16 для мешей <= 65535 вершин, иначе UInt32
    const IndexFormat target_index_format = (v_count <= 65535) ? IndexFormat::UInt16 : IndexFormat::UInt32;
    const uint32_t index_stride = GetIndexStride(target_index_format);

    eastl::vector<uint8_t> index_buffer(index_count * index_stride);

    if (input.indices.data && input.indices.format != IndexFormat::None) {
        for (uint32_t id = 0; id < index_count; ++id) {
            const uint8_t* raw_idx = input.indices.data + (id * input.indices.stride);
            uint32_t val = 0;
            switch (input.indices.format) {
                case IndexFormat::UInt16: val = *reinterpret_cast<const uint16_t*>(raw_idx); break;
                case IndexFormat::UInt32: val = *reinterpret_cast<const uint32_t*>(raw_idx); break;
                case IndexFormat::UInt8:  val = *raw_idx; break;
                default: break;
            }

            if (target_index_format == IndexFormat::UInt16) {
                reinterpret_cast<uint16_t*>(index_buffer.data())[id] = static_cast<uint16_t>(val);
            } else {
                reinterpret_cast<uint32_t*>(index_buffer.data())[id] = val;
            }
        }
    } else {
        if (target_index_format == IndexFormat::UInt16) {
            auto* dst = reinterpret_cast<uint16_t*>(index_buffer.data());
            for (uint32_t id = 0; id < index_count; ++id) {
                dst[id] = static_cast<uint16_t>(id);
            }
        } else {
            auto* dst = reinterpret_cast<uint32_t*>(index_buffer.data());
            for (uint32_t id = 0; id < index_count; ++id) {
                dst[id] = id;
            }
        }
    }

    header.index_format = target_index_format;
    header.index_count = index_count;

    return MeshBinary::Pack(header, vertex_buffer, index_buffer);
}

} // namespace tryeditor