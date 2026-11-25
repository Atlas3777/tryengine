#pragma once

#include <EASTL/span.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/vector.h>
#include <cstdint>

#include "../Vertex.hpp"
#include "engine/core/Result.hpp"

namespace tryengine::resources {

constexpr uint32_t GetIndexStride(IndexFormat format) {
    switch (format) {
        case IndexFormat::UInt16:
            return sizeof(uint16_t);
        case IndexFormat::UInt32:
            return sizeof(uint32_t);
        case IndexFormat::UInt8:
            return sizeof(uint8_t);
        default:
            TRY_ASSERT(false, "Invalid index format");
    }
    return 0;
}

// Position убран — описывает ТОЛЬКО упакованные атрибуты во 2-м буфере
enum class MeshAttributeFlags : uint32_t {
    None = 0,
    Normal = 1 << 0,     // int8_t[4] / SNorm8x4 (4 байта)
    Tangent = 1 << 1,    // int8_t[4] / SNorm8x4 (4 байта)
    TexCoord0 = 1 << 2,  // uint16_t[2] / Half2 (4 байта)
    TexCoord1 = 1 << 3,  // uint16_t[2] / Half2 (4 байта)
    Color0 = 1 << 4,     // uint8_t[4] / UNorm8x4 (4 байта)
    Joints0 = 1 << 5,    // uint8_t[4] (4 байта)
    Weights0 = 1 << 6,   // uint8_t[4] / UNorm8x4 (4 байта)
};

constexpr uint32_t GetPositionStride() {
    return sizeof(float) * 3;  // 12 байт (всегда в отдельном буфере)
}

// Имя атрибута для человека (`normal`, `texcoord0`, ...). Позиция живёт отдельным буфером
// и в маске не участвует, поэтому её здесь нет.
// Сентинелы в той же маске, что и MeshAttributeFlags. Оба значения не пересекаются с
// реальными битами (0..6), поэтому их можно хранить в одном uint.
//   pending    — шейдер ещё не загружен, совместимость неизвестна;
//   not mesh   — шейдер не для меша (UI, экранный проход), материал нельзя предлагать.
inline constexpr uint32_t kPendingMeshAttributes = 0xFFFFFFFFu;
inline constexpr uint32_t kNotMeshMaterialAttributes = 0xFFFFFFFEu;

inline const char* MeshAttributeLabel(MeshAttributeFlags flag) {
    switch (flag) {
        case MeshAttributeFlags::Normal:
            return "normal";
        case MeshAttributeFlags::Tangent:
            return "tangent";
        case MeshAttributeFlags::TexCoord0:
            return "texcoord0";
        case MeshAttributeFlags::TexCoord1:
            return "texcoord1";
        case MeshAttributeFlags::Color0:
            return "color0";
        case MeshAttributeFlags::Joints0:
            return "joints0";
        case MeshAttributeFlags::Weights0:
            return "weights0";
        case MeshAttributeFlags::None:
            break;
    }
    return "none";
}

// Токен, которым шейдер объявляет атрибут в `MESH_ATTRIBUTES` (см. ShaderCompilerSystem).
// Один список на парсер и на подсказки в редакторе.
inline bool ParseMeshAttributeToken(eastl::string_view token, MeshAttributeFlags& out) {
    if (token == "ATTR_BIT_NORMAL") {
        out = MeshAttributeFlags::Normal;
        return true;
    }
    if (token == "ATTR_BIT_TANGENT") {
        out = MeshAttributeFlags::Tangent;
        return true;
    }
    if (token == "ATTR_BIT_TEXCOORD0") {
        out = MeshAttributeFlags::TexCoord0;
        return true;
    }
    if (token == "ATTR_BIT_TEXCOORD1") {
        out = MeshAttributeFlags::TexCoord1;
        return true;
    }
    if (token == "ATTR_BIT_COLOR0") {
        out = MeshAttributeFlags::Color0;
        return true;
    }
    if (token == "ATTR_BIT_JOINTS0") {
        out = MeshAttributeFlags::Joints0;
        return true;
    }
    if (token == "ATTR_BIT_WEIGHTS0") {
        out = MeshAttributeFlags::Weights0;
        return true;
    }
    return false;
}

// Маска -> "normal, texcoord0". Пустая маска — "position only".
inline eastl::string DescribeMeshAttributeFlags(uint32_t mask) {
    if (mask == kPendingMeshAttributes)
        return "shader loading";
    if (mask == kNotMeshMaterialAttributes)
        return "not a mesh material";

    eastl::string text;
    for (MeshAttributeFlags flag :
         {MeshAttributeFlags::Normal, MeshAttributeFlags::Tangent, MeshAttributeFlags::TexCoord0,
          MeshAttributeFlags::TexCoord1, MeshAttributeFlags::Color0, MeshAttributeFlags::Joints0,
          MeshAttributeFlags::Weights0}) {
        if ((mask & static_cast<uint32_t>(flag)) == 0)
            continue;
        if (!text.empty())
            text.append(", ");
        text.append(MeshAttributeLabel(flag));
    }
    if (text.empty())
        text = "position only";
    return text;
}

// Вычисление шага вершины в Attribute Buffer (Normal, UV, Color и т.д.)
inline uint32_t CalculateAttributeBufferStride(MeshAttributeFlags flags) {
    const auto mask = static_cast<uint32_t>(flags);
    uint32_t stride = 0;
    if (mask & static_cast<uint32_t>(MeshAttributeFlags::Normal))
        stride += 4;
    if (mask & static_cast<uint32_t>(MeshAttributeFlags::Tangent))
        stride += 4;
    if (mask & static_cast<uint32_t>(MeshAttributeFlags::TexCoord0))
        stride += 4;
    if (mask & static_cast<uint32_t>(MeshAttributeFlags::TexCoord1))
        stride += 4;
    if (mask & static_cast<uint32_t>(MeshAttributeFlags::Color0))
        stride += 4;
    if (mask & static_cast<uint32_t>(MeshAttributeFlags::Joints0))
        stride += 4;
    if (mask & static_cast<uint32_t>(MeshAttributeFlags::Weights0))
        stride += 4;
    return stride;
}

// Вычисление суммарного размера данных одной вершины (Position Stream + Attribute Stream)
inline uint32_t CalculateTotalVertexStride(MeshAttributeFlags flags) {
    return GetPositionStride() + CalculateAttributeBufferStride(flags);
}

constexpr uint32_t MeshMagic = 0x4853454D;  // 'M','E','S','H'
constexpr uint32_t MeshVersion = 4;         // добавлен default_material

struct MeshHeader {
    uint32_t magic = MeshMagic;
    uint32_t version = MeshVersion;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;

    MeshAttributeFlags attribute_flags = MeshAttributeFlags::None;
    uint16_t attribute_stride = 0;  // Шаг одной вершины в attribute_buffer
    IndexFormat index_format = IndexFormat::None;
    uint8_t padding = 0;

    float bbox_min[3]{0.0f, 0.0f, 0.0f};
    float bbox_max[3]{0.0f, 0.0f, 0.0f};
    float radius = 0.0f;

    // Материал по умолчанию: id материала, с которым примитив пришёл из источника.
    // Гид меша адресует только геометрию; это данные артефакта, а не идентичность, поэтому
    // переимпорт источника обновляет таблицу, а авторский оверрайд (`ModelRender.material`)
    // живёт отдельно и адресуется путём. 0 — материала нет.
    uint32_t padding2 = 0;  // явное выравнивание до 8 байт
    uint64_t default_material = 0;
};

static_assert(sizeof(MeshHeader) == 64, "MeshHeader must be 64 bytes");

struct UnpackedMeshView {
    MeshHeader header;
    eastl::span<const uint8_t> position_data;   // Stream 0: Позиции (12 байт/вершина)
    eastl::span<const uint8_t> attribute_data;  // Stream 1: Атрибуты (attribute_stride байт/вершина)
    eastl::span<const uint8_t> index_data;      // Индексы
};

class MeshBinary {
public:
    static eastl::vector<uint8_t> Pack(const MeshHeader& header, eastl::span<const uint8_t> position_bytes,
                                       eastl::span<const uint8_t> attribute_bytes,
                                       eastl::span<const uint8_t> index_bytes);

    //! Только шапка: сборщику пака нужен `default_material` меша, а не его вершины.
    static Result<MeshHeader> UnpackHeader(eastl::span<const uint8_t> raw_bytes);

    static Result<UnpackedMeshView> Unpack(eastl::span<const uint8_t> raw_bytes);
};

}  // namespace tryengine::resources