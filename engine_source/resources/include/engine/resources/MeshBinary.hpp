#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>
#include <cstdint>

#include "Vertex.hpp"
#include "engine/core/Result.hpp"

namespace tryengine::resources {

constexpr uint32_t GetVertexStride(VertexFormat format) {
    switch (format) {
        case VertexFormat::Standard:        return sizeof(Vertex);               // 48
        case VertexFormat::StaticPacked:    return sizeof(VertexStaticPacked);   // 32
        case VertexFormat::SkinnedPacked:   return sizeof(VertexSkinnedPacked);  // 32
        case VertexFormat::SkinnedStandard: return sizeof(VertexSkinned);       // 64 TODO : VertexSkinned надо изменить
        case VertexFormat::PositionOnly:    return sizeof(VertexPositionOnly);   // 12
        case VertexFormat::Ui2D:            return sizeof(Vertex2D);             // 20
    }
    TRY_ASSERT(false, "Invalid vertex format");
    return 0;
}
static_assert(sizeof(Vertex) == 48);
static_assert(sizeof(VertexStaticPacked) == 32);
static_assert(sizeof(VertexSkinnedPacked) == 32);
// static_assert(sizeof(VertexSkinned) == 64); не верно
static_assert(sizeof(VertexPositionOnly) == 12);
static_assert(sizeof(Vertex2D) == 20);


constexpr uint32_t GetIndexStride(IndexFormat format) {
    switch (format) {
        case IndexFormat::UInt16: return sizeof(uint16_t); // 2 байта
        case IndexFormat::UInt32: return sizeof(uint32_t); // 4 байта
        case IndexFormat::UInt8:  return sizeof(uint8_t);  // 1 байт
        default: TRY_ASSERT(false, "Invalid index format");
    }
    return sizeof(uint16_t);
}

struct MeshHeader {
    uint32_t magic = 0x4853454D; // 'M','E','S','H' в Little-Endian
    uint32_t version = 1;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;

    VertexFormat vertex_format = VertexFormat::Standard;
    IndexFormat index_format = IndexFormat::UInt16;
    uint8_t padding[2];

    float bbox_min[3]{0.0f, 0.0f, 0.0f};
    float bbox_max[3]{0.0f, 0.0f, 0.0f};

    uint8_t reserved[4];
};

static_assert(sizeof(MeshHeader) == 48, "MeshHeader must be 48 bytes");

struct UnpackedMeshView {
    MeshHeader header;
    eastl::span<const uint8_t> vertex_data;
    eastl::span<const uint8_t> index_data;
};

class MeshBinary {
public:
    static eastl::vector<uint8_t> Pack(const MeshHeader& header,
                                       eastl::span<const uint8_t> vertex_bytes,
                                       eastl::span<const uint8_t> indices);

    static Result<UnpackedMeshView> Unpack(eastl::span<const uint8_t> raw_bytes);
};

}  // namespace tryengine::resources