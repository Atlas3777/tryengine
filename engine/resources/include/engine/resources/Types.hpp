#pragma once

#include <cstdint>
#include <vector>

namespace tryengine::resources {

struct Vertex {
    float x, y, z;
    float nx, ny, nz;
    float r, g, b, a;
    float u, v;
};

struct MeshData {
    std::vector<Vertex> vertexBuffer;
    std::vector<uint32_t> indexBuffer;
};

struct TextureData {
    uint32_t width;
    uint32_t height;
    uint32_t channels;  // Добавили информацию о каналах
    std::vector<uint8_t> pixels;
};

struct TextureHeader {
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t dataSize;
};

}  // namespace tryengine::resources