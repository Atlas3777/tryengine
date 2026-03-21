#pragma once

namespace engine::resources {

struct Vertex {
    float x, y, z;
    float nx, ny, nz;
    float r, g, b, a;
    float u, v;
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};

struct TextureData {
    int width;
    int height;
    std::vector<uint8_t> pixels;
};

}