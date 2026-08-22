#pragma once

#include <cstdint>

namespace tryengine::resources {

struct Vertex {
    float x, y, z;
    float nx, ny, nz;
    float r, g, b, a;
    float u, v;
};

struct VertexSkinned {
    float x, y, z;
    float nx, ny, nz;
    float r, g, b, a;
    float u, v;
    uint32_t boneIndices[4];
    float boneWeights[4];
};

struct VertexPositionOnly {
    float x, y, z;
};

struct Vertex2D {
    float x, y;
    float u, v;
    uint8_t r, g, b, a;
};

// Выравнивание по 16 байт гарантирует идеальное чтение из GPU VRAM
struct alignas(16) VertexStaticPacked {
    float position[3];   // 12 байт | location = 0 (FLOAT3)
    int8_t normal[4];    //  4 байта | location = 1 (BYTE4_NORM)
    uint8_t color[4];    //  4 байта | location = 2 (UBYTE4_NORM)
    uint16_t uv[2];      //  4 байта | location = 3 (HALF2)
    uint8_t padding[8];  //  8 байт  | Выравнивание до 32 байт
};  // ИТОГО: 32 байта

struct alignas(16) VertexSkinnedPacked {
    float position[3];  // 12 байт | location = 0
    uint32_t normal;    //  4 байта | location = 1
    uint8_t color[4];   //  4 байта | location = 2
    uint16_t uv[2];     //  4 байта | location = 3

    uint8_t boneIndices[4];  //  4 байта | location = 4 (R8G8B8A8_UINT)
    uint8_t boneWeights[4];  //  4 байта | location = 5 (R8G8B8A8_UNORM)
};
// ИТОГО: 32 байта

enum class VertexFormat : uint8_t {
    Standard,        // resources::Vertex (48 байт)
    StaticPacked,    // VertexStaticPacked (32 байта)
    SkinnedPacked,   // VertexSkinnedPacked (32 байта)
    SkinnedStandard, // VertexSkinned (64 байта) // переделать.
    PositionOnly,    // VertexPositionOnly (12 байт)
    Ui2D             // Vertex2D (20 байт)
};

enum class IndexFormat : uint8_t {
    None,
    UInt8,
    UInt16,
    UInt32
};

}  // namespace tryengine::resources