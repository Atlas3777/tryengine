#include "engine/resources/TextureBinary.hpp"

#include <EASTL/vector.h>
#include <cstring>

#include "engine/core/MakeError.hpp"

namespace tryengine::resources {

namespace {

struct FormatDetails {
    bool is_compressed{false};
    uint32_t block_width{1};
    uint32_t block_height{1};
    uint32_t bytes_per_block{0};
};

FormatDetails GetFormatDetails(TextureFormat format) {
    switch (format) {
        // --- 8-bit / channel (1 byte / pixel) ---
        case TextureFormat::VK_FORMAT_R8_UNORM:
        case TextureFormat::VK_FORMAT_R8_UINT:
        case TextureFormat::VK_FORMAT_R8_SRGB:
            return {.is_compressed = false, .block_width = 1, .block_height = 1, .bytes_per_block = 1};

        // --- 16-bit / channel or 2-channel 8-bit (2 bytes / pixel) ---
        case TextureFormat::VK_FORMAT_R8G8_UNORM:
        case TextureFormat::VK_FORMAT_R8G8_SRGB:
        case TextureFormat::VK_FORMAT_R16_UNORM:
        case TextureFormat::VK_FORMAT_R16_SFLOAT:
        case TextureFormat::VK_FORMAT_D16_UNORM:
            return {.is_compressed = false, .block_width = 1, .block_height = 1, .bytes_per_block = 2};

        // --- 32-bit (4 bytes / pixel) ---
        case TextureFormat::VK_FORMAT_R8G8B8A8_UNORM:
        case TextureFormat::VK_FORMAT_R8G8B8A8_SRGB:
        case TextureFormat::VK_FORMAT_B8G8R8A8_UNORM:
        case TextureFormat::VK_FORMAT_B8G8R8A8_SRGB:
        case TextureFormat::VK_FORMAT_R16G16_UNORM:
        case TextureFormat::VK_FORMAT_R16G16_SFLOAT:
        case TextureFormat::VK_FORMAT_R32_SFLOAT:
        case TextureFormat::VK_FORMAT_B10G11R11_UFLOAT_PACK32:
        case TextureFormat::VK_FORMAT_D32_SFLOAT:
        case TextureFormat::VK_FORMAT_D24_UNORM_S8_UINT:
            return {.is_compressed = false, .block_width = 1, .block_height = 1, .bytes_per_block = 4};

        // --- 64-bit (8 bytes / pixel) ---
        case TextureFormat::VK_FORMAT_R16G16B16A16_UNORM:
        case TextureFormat::VK_FORMAT_R16G16B16A16_SFLOAT:
        case TextureFormat::VK_FORMAT_R32G32_SFLOAT:
        case TextureFormat::VK_FORMAT_D32_SFLOAT_S8_UINT:
            return {.is_compressed = false, .block_width = 1, .block_height = 1, .bytes_per_block = 8};

        // --- 128-bit (16 bytes / pixel) ---
        case TextureFormat::VK_FORMAT_R32G32B32A32_SFLOAT:
            return {.is_compressed = false, .block_width = 1, .block_height = 1, .bytes_per_block = 16};

        // --- BC1 & BC4 (8 bytes per 4x4 block) ---
        case TextureFormat::VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case TextureFormat::VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case TextureFormat::VK_FORMAT_BC4_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC4_SNORM_BLOCK:
            return {.is_compressed = true, .block_width = 4, .block_height = 4, .bytes_per_block = 8};

        // --- BC2, BC3, BC5, BC6H, BC7 (16 bytes per 4x4 block) ---
        case TextureFormat::VK_FORMAT_BC2_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC2_SRGB_BLOCK:
        case TextureFormat::VK_FORMAT_BC3_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC3_SRGB_BLOCK:
        case TextureFormat::VK_FORMAT_BC5_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC5_SNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case TextureFormat::VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case TextureFormat::VK_FORMAT_BC7_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC7_SRGB_BLOCK:
            return {.is_compressed = true, .block_width = 4, .block_height = 4, .bytes_per_block = 16};

        default:
            TRY_ASSERT(false, "Unsupported TextureFormat in GetFormatDetails");
            return {.is_compressed = false, .block_width = 1, .block_height = 1, .bytes_per_block = 4};
    }
}

uint64_t GetMipSize(uint32_t w, uint32_t h, uint32_t d, TextureFormat format) {
    FormatDetails details = GetFormatDetails(format);

    if (details.is_compressed) {
        uint32_t blocks_x = (w + details.block_width - 1) / details.block_width;
        uint32_t blocks_y = (h + details.block_height - 1) / details.block_height;
        uint32_t blocks_z = (d > 1) ? d : 1;
        return static_cast<uint64_t>(blocks_x) * blocks_y * blocks_z * details.bytes_per_block;
    }

    return static_cast<uint64_t>(w) * h * d * details.bytes_per_block;
}

} // namespace

eastl::vector<uint8_t> TextureBinary::Pack(const TextureHeader& header,
                                           eastl::span<const uint8_t> full_data) {
    uint32_t mip_count = header.mip_count;

    uint64_t mip_table_offset = sizeof(TextureHeader);
    uint64_t mip_table_size = static_cast<uint64_t>(mip_count) * sizeof(MipInfo);
    uint64_t data_offset = mip_table_offset + mip_table_size;

    eastl::vector<MipInfo> mip_table(mip_count);
    uint64_t current_data_offset = data_offset;

    uint32_t temp_w = header.width;
    uint32_t temp_h = header.height;
    uint32_t temp_d = header.depth;

    for (uint32_t i = 0; i < mip_count; ++i) {
        uint64_t mip_size = GetMipSize(temp_w, temp_h, temp_d, header.format);

        mip_table[i].offset = current_data_offset;
        mip_table[i].size = mip_size;

        current_data_offset += mip_size;
        temp_w = (temp_w > 1) ? (temp_w >> 1) : 1;
        temp_h = (temp_h > 1) ? (temp_h >> 1) : 1;
        temp_d = (temp_d > 1) ? (temp_d >> 1) : 1;
    }

    uint64_t total_file_size = current_data_offset;
    eastl::vector<uint8_t> file_buffer(total_file_size);
    uint8_t* write_ptr = file_buffer.data();

    TextureHeader final_header = header;
    final_header.mip_table_offset = mip_table_offset;
    final_header.mip_table_size = mip_table_size;

    // 1. Заголовок
    std::memcpy(write_ptr, &final_header, sizeof(TextureHeader));
    write_ptr += sizeof(TextureHeader);

    // 2. Таблица мипов
    std::memcpy(write_ptr, mip_table.data(), mip_table_size);
    write_ptr += mip_table_size;

    // 3. Данные текстуры
    std::memcpy(write_ptr, full_data.data(), full_data.size());

    return file_buffer;
}

Result<UnpackedTextureView> TextureBinary::UnpackFull(eastl::span<const uint8_t> raw_bytes) {
    if (raw_bytes.size() < sizeof(TextureHeader)) {
        return LogAndMakeError("Raw bytes too small for TextureHeader");
    }

    UnpackedTextureView view;
    std::memcpy(&view.header, raw_bytes.data(), sizeof(TextureHeader));

    if (view.header.magic != TextureMagic)
        return LogAndMakeError("Invalid magic number");

    if (view.header.version != TextureVersion)
        return LogAndMakeError("Unsupported texture version");

    uint64_t mip_table_offset = view.header.mip_table_offset;
    uint64_t mip_table_size = view.header.mip_table_size;

    if (raw_bytes.size() < mip_table_offset + mip_table_size)
        return LogAndMakeError("Raw bytes truncated at MipTable");

    uint32_t mip_count = view.header.mip_count;
    view.mips.reserve(mip_count);

    const MipInfo* file_mip_table = reinterpret_cast<const MipInfo*>(
        raw_bytes.data() + mip_table_offset
    );

    uint32_t curr_w = view.header.width;
    uint32_t curr_h = view.header.height;
    uint32_t curr_d = view.header.depth;

    for (uint32_t i = 0; i < mip_count; ++i) {
        const MipInfo& info = file_mip_table[i];

        if (info.offset + info.size > raw_bytes.size())
            return LogAndMakeError("Mip data out of bounds");

        MipView mip;
        mip.width = curr_w;
        mip.height = curr_h;
        mip.depth = curr_d;
        mip.size = info.size;
        mip.data = eastl::span<const uint8_t>(raw_bytes.data() + info.offset, info.size);

        view.mips.push_back(mip);

        curr_w = (curr_w > 1) ? (curr_w >> 1) : 1;
        curr_h = (curr_h > 1) ? (curr_h >> 1) : 1;
        curr_d = (curr_d > 1) ? (curr_d >> 1) : 1;
    }

    return view;
}

} // namespace tryengine::resources