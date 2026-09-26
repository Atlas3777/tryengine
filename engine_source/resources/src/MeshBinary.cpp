#include "engine/resources/MeshBinary.hpp"
#include <cstring>
#include "engine/core/MakeError.hpp"

namespace tryengine::resources {

eastl::vector<uint8_t> MeshBinary::Pack(const MeshHeader& header,
                                        eastl::span<const uint8_t> position_bytes,
                                        eastl::span<const uint8_t> attribute_bytes,
                                        eastl::span<const uint8_t> index_bytes) {
    MeshHeader final_header = header;
    final_header.vertex_count = static_cast<uint32_t>(position_bytes.size() / (sizeof(float) * 3));
    final_header.index_count = static_cast<uint32_t>(index_bytes.size() / GetIndexStride(header.index_format));
    final_header.attribute_stride = static_cast<uint16_t>(CalculateAttributeBufferStride(header.attribute_flags));
    const size_t total_size = sizeof(MeshHeader) + position_bytes.size() + attribute_bytes.size() + index_bytes.size();

    eastl::vector<uint8_t> buffer(total_size);
    uint8_t* write_ptr = buffer.data();

    std::memcpy(write_ptr, &final_header, sizeof(MeshHeader));
    write_ptr += sizeof(MeshHeader);

    if (!position_bytes.empty()) {
        std::memcpy(write_ptr, position_bytes.data(), position_bytes.size());
        write_ptr += position_bytes.size();
    }

    if (!attribute_bytes.empty()) {
        std::memcpy(write_ptr, attribute_bytes.data(), attribute_bytes.size());
        write_ptr += attribute_bytes.size();
    }

    if (!index_bytes.empty()) {
        std::memcpy(write_ptr, index_bytes.data(), index_bytes.size());
    }

    return buffer;
}

Result<UnpackedMeshView> MeshBinary::Unpack(eastl::span<const uint8_t> raw_bytes) {
    if (raw_bytes.size() < sizeof(MeshHeader)) {
        return LogAndMakeError("MeshBinary: Buffer too small for header");
    }

    UnpackedMeshView view{};
    std::memcpy(&view.header, raw_bytes.data(), sizeof(MeshHeader));

    if (view.header.magic != MeshMagic) {
        return LogAndMakeError("MeshBinary: Invalid magic bytes");
    }

    if (view.header.version != MeshVersion) {
        return LogAndMakeError("MeshBinary: Unsupported version {}", view.header.version);
    }

    const size_t pos_bytes = view.header.vertex_count * (sizeof(float) * 3);
    const size_t attr_bytes = view.header.vertex_count * view.header.attribute_stride;
    const size_t idx_bytes = view.header.index_count * GetIndexStride(view.header.index_format);

    if (raw_bytes.size() < sizeof(MeshHeader) + pos_bytes + attr_bytes + idx_bytes) {
        return LogAndMakeError("MeshBinary: Corrupted payload size");
    }

    const uint8_t* ptr = raw_bytes.data() + sizeof(MeshHeader);
    view.position_data = eastl::span<const uint8_t>(ptr, pos_bytes);
    ptr += pos_bytes;

    view.attribute_data = eastl::span<const uint8_t>(ptr, attr_bytes);
    ptr += attr_bytes;

    if (view.header.index_count > 0) {
        view.index_data = eastl::span<const uint8_t>(ptr, idx_bytes);
    }

    return view;
}

} // namespace tryengine::resources