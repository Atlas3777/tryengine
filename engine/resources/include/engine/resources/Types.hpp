#pragma once

#include <cstdint>
#include <vector>

namespace tryengine::resources {

enum class TextureFilter : uint8_t { Nearest = 0, Linear = 1 };
enum class TextureAddressMode : uint8_t { Repeat = 0, MirroredRepeat = 1, ClampToEdge = 2 };

// Заголовок бинарного артефакта (.tex)
struct TextureHeader {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;
    uint32_t data_size = 0;
    // Настройки сэмплера запекаются прямо сюда
    TextureFilter min_filter = TextureFilter::Linear;
    TextureFilter mag_filter = TextureFilter::Linear;
    TextureAddressMode address_u = TextureAddressMode::Repeat;
    TextureAddressMode address_v = TextureAddressMode::Repeat;
};

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


}  // namespace tryengine::resources