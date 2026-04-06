#pragma once

#include <vector>

namespace engine::resources {

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
    std::vector<uint8_t> pixels;
};

}