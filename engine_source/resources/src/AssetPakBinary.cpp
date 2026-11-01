#include "../include/engine/resources/binary/AssetPakBinary.hpp"

#include <cstring>

#include "engine/core/MakeError.hpp"

namespace tryengine::resources {
namespace {

// Проверка, что диапазон [off, off + size) полностью лежит внутри буфера
// без риска переполнения uint64_t.
inline bool FitsInBuffer(uint64_t off, uint64_t size, size_t buffer_size) {
    const auto buf = static_cast<uint64_t>(buffer_size);
    return off <= buf && size <= buf - off;
}

}  // namespace

Result<AssetPakHeader> AssetPakBinary::ReadHeader(eastl::span<const uint8_t> buffer) {
    if (buffer.size() < sizeof(AssetPakHeader))
        return LogAndMakeError("Buffer too small for AssetPakHeader");

    AssetPakHeader header;
    std::memcpy(&header, buffer.data(), sizeof(AssetPakHeader));

    if (header.magic != PAK_MAGIC)
        return LogAndMakeError("Invalid PAK magic header");

    if (header.version != PAK_VERSION)
        return LogAndMakeError("Unsupported PAK version {}", header.version);

    return header;
}

Result<AssetPakManifest> AssetPakBinary::UnpackManifest(eastl::span<const uint8_t> buffer) {
    auto header_res = ReadHeader(buffer);
    if (!header_res.has_value())
        return eastl::move(header_res.error());

    AssetPakManifest meta;
    meta.header = *header_res;

    if (!FitsInBuffer(meta.header.dependencies_offset, meta.header.dependencies_size, buffer.size()) ||
        !FitsInBuffer(meta.header.resource_layout_offset, meta.header.resource_layout_size, buffer.size())) {
        return LogAndMakeError("PAK metadata offsets out of buffer bounds");
    }

    if (meta.header.resource_layout_size % sizeof(ResourceLayout) != 0) {
        return LogAndMakeError("PAK resource layout size is not a multiple of ResourceLayout");
    }

    // 2. Распаковка зависимостей (с проверками границ на каждом шаге).
    if (meta.header.dependencies_size > 0) {
        const uint8_t* base = buffer.data() + meta.header.dependencies_offset;
        const size_t section_size = static_cast<size_t>(meta.header.dependencies_size);
        size_t off = 0;

        if (section_size < sizeof(uint32_t))
            return LogAndMakeError("PAK dependencies section too small for count");

        uint32_t count = 0;
        std::memcpy(&count, base + off, sizeof(count));
        off += sizeof(count);

        // Каждый элемент требует как минимум 4 байта под длину.
        if (static_cast<size_t>(count) * sizeof(uint32_t) > section_size - off)
            return LogAndMakeError("PAK dependency count exceeds section size");

        meta.dependencies.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            if (off + sizeof(uint32_t) > section_size)
                return LogAndMakeError("PAK dependency length out of bounds");

            uint32_t len = 0;
            std::memcpy(&len, base + off, sizeof(len));
            off += sizeof(len);

            if (len > section_size - off)
                return LogAndMakeError("PAK dependency string out of bounds");

            meta.dependencies.emplace_back(reinterpret_cast<const char*>(base + off), len);
            off += len;
        }
    }

    // 3. Распаковка ResourceLayout (после проверки кратности размеру записи).
    if (meta.header.resource_layout_size > 0) {
        const uint32_t layout_count = static_cast<uint32_t>(meta.header.resource_layout_size / sizeof(ResourceLayout));
        meta.layout.resize(layout_count);
        std::memcpy(meta.layout.data(), buffer.data() + meta.header.resource_layout_offset,
                    static_cast<size_t>(meta.header.resource_layout_size));
    }

    return meta;
}

eastl::vector<uint8_t> AssetPakBinary::Pack(AssetPak& pak) {
    // 1. Сериализуем зависимости (requirement_paks).
    eastl::vector<uint8_t> deps_bytes;

    if (!pak.requirement_paks.empty()) {
        const uint32_t dep_count = static_cast<uint32_t>(pak.requirement_paks.size());
        deps_bytes.insert(deps_bytes.end(), reinterpret_cast<const uint8_t*>(&dep_count),
                          reinterpret_cast<const uint8_t*>(&dep_count) + sizeof(dep_count));

        for (const auto& dep : pak.requirement_paks) {
            const uint32_t len = static_cast<uint32_t>(dep.size());
            deps_bytes.insert(deps_bytes.end(), reinterpret_cast<const uint8_t*>(&len),
                              reinterpret_cast<const uint8_t*>(&len) + sizeof(len));
            deps_bytes.insert(deps_bytes.end(), reinterpret_cast<const uint8_t*>(dep.data()),
                              reinterpret_cast<const uint8_t*>(dep.data()) + len);
        }
    }

    // 2. Строим таблицу ResourceLayout и склеенный буфер данных.
    eastl::vector<ResourceLayout> layout;
    // Резервируем место под все типы ресурсов
    layout.reserve(pak.meshes.size() + pak.mats.size() + pak.shaders.size() + pak.textures.size());
    eastl::vector<uint8_t> data_buffer;

    auto append_resource = [&](uint64_t guid, const eastl::vector<uint8_t>& bytes) {
        ResourceLayout entry;
        entry.guid = guid;
        entry.offset = static_cast<uint64_t>(data_buffer.size());
        entry.size = static_cast<uint64_t>(bytes.size());
        data_buffer.insert(data_buffer.end(), bytes.begin(), bytes.end());
        layout.push_back(entry);
    };

    // Берём min(вектор.size(), вектор_data.size()), чтобы не выйти за границы
    // при возможном рассинхроне параллельных массивов.

    const size_t mesh_count = pak.meshes.size() < pak.meshes_data.size() ? pak.meshes.size() : pak.meshes_data.size();
    for (size_t i = 0; i < mesh_count; ++i)
        append_resource(pak.meshes[i], pak.meshes_data[i]);

    const size_t mat_count = pak.mats.size() < pak.mats_data.size() ? pak.mats.size() : pak.mats_data.size();
    for (size_t i = 0; i < mat_count; ++i)
        append_resource(pak.mats[i], pak.mats_data[i]);

    // Добавляем сохранение шейдеров
    const size_t shader_count =
        pak.shaders.size() < pak.shaders_data.size() ? pak.shaders.size() : pak.shaders_data.size();
    for (size_t i = 0; i < shader_count; ++i)
        append_resource(pak.shaders[i], pak.shaders_data[i]);

    // Добавляем сохранение текстур
    const size_t texture_count =
        pak.textures.size() < pak.textures_data.size() ? pak.textures.size() : pak.textures_data.size();
    for (size_t i = 0; i < texture_count; ++i)
        append_resource(pak.textures[i], pak.textures_data[i]);

    // 3. Формируем заголовок.
    AssetPakHeader header{};
    header.magic = PAK_MAGIC;
    header.version = PAK_VERSION;

    header.dependencies_offset = sizeof(AssetPakHeader);
    header.dependencies_size = static_cast<uint64_t>(deps_bytes.size());

    header.resource_layout_offset = header.dependencies_offset + header.dependencies_size;
    header.resource_layout_size = static_cast<uint64_t>(layout.size() * sizeof(ResourceLayout));

    header.data_offset = header.resource_layout_offset + header.resource_layout_size;
    header.data_size = static_cast<uint64_t>(data_buffer.size());

    // 4. Собираем итоговый буфер.
    eastl::vector<uint8_t> result(static_cast<size_t>(header.data_offset + header.data_size));
    std::memcpy(result.data(), &header, sizeof(header));

    if (!deps_bytes.empty()) {
        std::memcpy(result.data() + header.dependencies_offset, deps_bytes.data(), deps_bytes.size());
    }
    if (!layout.empty()) {
        std::memcpy(result.data() + header.resource_layout_offset, layout.data(),
                    static_cast<size_t>(header.resource_layout_size));
    }
    if (!data_buffer.empty()) {
        std::memcpy(result.data() + header.data_offset, data_buffer.data(), data_buffer.size());
    }

    return result;
}

}  // namespace tryengine::resources