#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <cstdint>

#include "Vertex.hpp"
#include "engine/core/Result.hpp"

namespace tryengine::resources {

constexpr uint32_t GetIndexStride(IndexFormat format) {
    switch (format) {
        case IndexFormat::UInt16: return sizeof(uint16_t);
        case IndexFormat::UInt32: return sizeof(uint32_t);
        case IndexFormat::UInt8:  return sizeof(uint8_t);
        default: TRY_ASSERT(false, "Invalid index format");
    }
    return 0;
}

// Position убран — описывает ТОЛЬКО упакованные атрибуты во 2-м буфере
enum class MeshAttributeFlags : uint32_t {
    None        = 0,
    Normal      = 1 << 0, // int8_t[4] / SNorm8x4 (4 байта)
    Tangent     = 1 << 1, // int8_t[4] / SNorm8x4 (4 байта)
    TexCoord0   = 1 << 2, // uint16_t[2] / Half2 (4 байта)
    TexCoord1   = 1 << 3, // uint16_t[2] / Half2 (4 байта)
    Color0      = 1 << 4, // uint8_t[4] / UNorm8x4 (4 байта)
    Joints0     = 1 << 5, // uint8_t[4] (4 байта)
    Weights0    = 1 << 6, // uint8_t[4] / UNorm8x4 (4 байта)
};

constexpr uint32_t GetPositionStride() {
    return sizeof(float) * 3; // 12 байт (всегда в отдельном буфере)
}

// Вычисление шага вершины в Attribute Buffer (Normal, UV, Color и т.д.)
inline uint32_t CalculateAttributeBufferStride(MeshAttributeFlags flags) {
    const auto mask = static_cast<uint32_t>(flags);
    uint32_t stride = 0;
    if (mask & static_cast<uint32_t>(MeshAttributeFlags::Normal))    stride += 4;
    if (mask & static_cast<uint32_t>(MeshAttributeFlags::Tangent))   stride += 4;
    if (mask & static_cast<uint32_t>(MeshAttributeFlags::TexCoord0)) stride += 4;
    if (mask & static_cast<uint32_t>(MeshAttributeFlags::TexCoord1)) stride += 4;
    if (mask & static_cast<uint32_t>(MeshAttributeFlags::Color0))    stride += 4;
    if (mask & static_cast<uint32_t>(MeshAttributeFlags::Joints0))   stride += 4;
    if (mask & static_cast<uint32_t>(MeshAttributeFlags::Weights0))  stride += 4;
    return stride;
}

// Вычисление суммарного размера данных одной вершины (Position Stream + Attribute Stream)
inline uint32_t CalculateTotalVertexStride(MeshAttributeFlags flags) {
    return GetPositionStride() + CalculateAttributeBufferStride(flags);
}

constexpr uint32_t MeshMagic = 0x4853454D; // 'M','E','S','H'
constexpr uint32_t MeshVersion = 3; // убрал позицию

struct MeshHeader {
    uint32_t magic = MeshMagic;
    uint32_t version = MeshVersion;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;

    MeshAttributeFlags attribute_flags = MeshAttributeFlags::None;
    uint16_t attribute_stride = 0; // Шаг одной вершины в attribute_buffer
    IndexFormat index_format = IndexFormat::None;
    uint8_t padding = 0;

    float bbox_min[3]{0.0f, 0.0f, 0.0f};
    float bbox_max[3]{0.0f, 0.0f, 0.0f};
    float radius = 0.0f;
};

static_assert(sizeof(MeshHeader) == 52, "MeshHeader must be 52 bytes");

struct UnpackedMeshView {
    MeshHeader header;
    eastl::span<const uint8_t> position_data;  // Stream 0: Позиции (12 байт/вершина)
    eastl::span<const uint8_t> attribute_data; // Stream 1: Атрибуты (attribute_stride байт/вершина)
    eastl::span<const uint8_t> index_data;     // Индексы
};

class MeshBinary {
public:
    static eastl::vector<uint8_t> Pack(const MeshHeader& header,
                                       eastl::span<const uint8_t> position_bytes,
                                       eastl::span<const uint8_t> attribute_bytes,
                                       eastl::span<const uint8_t> index_bytes);

    static Result<UnpackedMeshView> Unpack(eastl::span<const uint8_t> raw_bytes);
};

} // namespace tryengine::resources