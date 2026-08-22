#include "engine/resources/MeshBinary.hpp"

#include <cstring>

#include "engine/core/MakeError.hpp"

namespace tryengine::resources {

eastl::vector<uint8_t> MeshBinary::Pack(const MeshHeader& header,
                                        eastl::span<const uint8_t> vertex_bytes,
                                        eastl::span<const uint8_t> index_bytes) {
    const uint32_t v_stride = GetVertexStride(header.vertex_format);
    const uint32_t i_stride = GetIndexStride(header.index_format);

    MeshHeader final_header = header;
    final_header.vertex_count = (v_stride > 0) ? static_cast<uint32_t>(vertex_bytes.size() / v_stride) : 0;
    final_header.index_count = (i_stride > 0) ? static_cast<uint32_t>(index_bytes.size() / i_stride) : 0;

    const size_t total_size = sizeof(MeshHeader) + vertex_bytes.size() + index_bytes.size();

    eastl::vector<uint8_t> buffer(total_size);
    uint8_t* write_ptr = buffer.data();

    std::memcpy(write_ptr, &final_header, sizeof(MeshHeader));
    write_ptr += sizeof(MeshHeader);

    if (!vertex_bytes.empty()) {
        std::memcpy(write_ptr, vertex_bytes.data(), vertex_bytes.size());
        write_ptr += vertex_bytes.size();
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

    if (view.header.magic != 0x4853454D) {
        return LogAndMakeError("MeshBinary: Invalid magic bytes");
    }

    if (view.header.version != 1) {
        return LogAndMakeError("MeshBinary: Unsupported version {}", view.header.version);
    }

    const uint32_t v_stride = GetVertexStride(view.header.vertex_format);
    const uint32_t i_stride = GetIndexStride(view.header.index_format);

    const size_t expected_vertex_bytes = view.header.vertex_count * v_stride;
    const size_t expected_index_bytes = view.header.index_count * i_stride;
    const size_t required_total = sizeof(MeshHeader) + expected_vertex_bytes + expected_index_bytes;

    if (raw_bytes.size() < required_total) {
        return LogAndMakeError("MeshBinary: Corrupted payload size");
    }

    const uint8_t* payload_ptr = raw_bytes.data() + sizeof(MeshHeader);

    view.vertex_data = eastl::span<const uint8_t>(payload_ptr, expected_vertex_bytes);

    if (view.header.index_count > 0) {
        view.index_data = eastl::span<const uint8_t>(
            payload_ptr + expected_vertex_bytes, expected_index_bytes);
    }

    return view;
}

}  // namespace tryengine::resources