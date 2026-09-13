#include "engine/resources/AssetPakBinary.hpp"

#include <cstring>

#include "engine/core/MakeError.hpp"

namespace tryengine::resources {

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

Result<AssetPakMetadata> AssetPakBinary::UnpackManifest(eastl::span<const uint8_t> buffer) {
    auto header_res = ReadHeader(buffer);
    if (!header_res.has_value()) {
        return eastl::move(header_res.error());
    }

    AssetPakMetadata meta;
    meta.header = *header_res;

    // 1. Проверка выходов за границы для таблиц
    if (meta.header.dependencies_offset + meta.header.dependencies_size > buffer.size() ||
        meta.header.resource_layout_offset + meta.header.resource_layout_size > buffer.size()) {
        return LogAndMakeError("PAK metadata offsets out of buffer bounds");
    }

    // 2. Распаковка зависимостей
    if (meta.header.dependencies_size > 0) {
        const uint8_t* ptr = buffer.data() + meta.header.dependencies_offset;
        uint32_t count = 0;
        std::memcpy(&count, ptr, sizeof(count));
        ptr += sizeof(count);

        meta.dependencies.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t len = 0;
            std::memcpy(&len, ptr, sizeof(len));
            ptr += sizeof(len);

            meta.dependencies.emplace_back(reinterpret_cast<const char*>(ptr), len);
            ptr += len;
        }
    }

    // 3. Распаковка ResourceLayout
    if (meta.header.resource_layout_size > 0) {
        const uint32_t layout_count = static_cast<uint32_t>(meta.header.resource_layout_size / sizeof(ResourceLayout));
        meta.layout.resize(layout_count);
        std::memcpy(meta.layout.data(), buffer.data() + meta.header.resource_layout_offset,
                    meta.header.resource_layout_size);
    }

    return meta;
}

eastl::vector<uint8_t> AssetPakBinary::Pack(const AssetPakMetadata& meta, eastl::span<const uint8_t> data_buffer) {
    eastl::vector<uint8_t> deps_bytes;
    const uint32_t dep_count = static_cast<uint32_t>(meta.dependencies.size());
    deps_bytes.insert(deps_bytes.end(), reinterpret_cast<const uint8_t*>(&dep_count),
                      reinterpret_cast<const uint8_t*>(&dep_count) + sizeof(dep_count));

    for (const auto& dep : meta.dependencies) {
        const uint32_t len = static_cast<uint32_t>(dep.size());
        deps_bytes.insert(deps_bytes.end(), reinterpret_cast<const uint8_t*>(&len),
                          reinterpret_cast<const uint8_t*>(&len) + sizeof(len));
        deps_bytes.insert(deps_bytes.end(), reinterpret_cast<const uint8_t*>(dep.data()),
                          reinterpret_cast<const uint8_t*>(dep.data()) + len);
    }

    AssetPakHeader header = meta.header;
    header.magic = PAK_MAGIC;
    header.version = PAK_VERSION;

    header.dependencies_offset = sizeof(AssetPakHeader);
    header.dependencies_size = deps_bytes.size();

    header.resource_layout_offset = header.dependencies_offset + header.dependencies_size;
    header.resource_layout_size = meta.layout.size() * sizeof(ResourceLayout);

    header.data_offset = header.resource_layout_offset + header.resource_layout_size;
    header.data_size = data_buffer.size();

    eastl::vector<uint8_t> result(header.data_offset + header.data_size);
    std::memcpy(result.data(), &header, sizeof(header));
    std::memcpy(result.data() + header.dependencies_offset, deps_bytes.data(), deps_bytes.size());
    std::memcpy(result.data() + header.resource_layout_offset, meta.layout.data(), header.resource_layout_size);
    if (!data_buffer.empty()) {
        std::memcpy(result.data() + header.data_offset, data_buffer.data(), data_buffer.size());
    }

    return result;
}

}  // namespace tryengine::resources