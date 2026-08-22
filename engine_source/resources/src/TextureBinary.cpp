#include "engine/resources/TextureBinary.hpp"

#include <EASTL/vector.h>
#include <cstring>

#include "engine/core/MakeError.hpp"

namespace tryengine::resources {

namespace {

uint32_t GetBytesPerBlock(TextureFormat format) {
    switch (format) {
        case TextureFormat::TEXTUREFORMAT_BC1_RGBA_UNORM:
        case TextureFormat::TEXTUREFORMAT_BC1_RGBA_UNORM_SRGB:
        case TextureFormat::TEXTUREFORMAT_BC4_R_UNORM:
            return 8;
        case TextureFormat::TEXTUREFORMAT_BC3_RGBA_UNORM:
        case TextureFormat::TEXTUREFORMAT_BC3_RGBA_UNORM_SRGB:
        case TextureFormat::TEXTUREFORMAT_BC5_RG_UNORM:
        case TextureFormat::TEXTUREFORMAT_BC7_RGBA_UNORM:
        case TextureFormat::TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB:
            return 16;
        default:
            TRY_ASSERT(false, "Invalid TextureFormat");
            return 4; // Fallback для несжатых форматов (например, RGBA8)
    }
}

uint64_t GetMipSize(uint32_t w, uint32_t h, uint32_t d, TextureFormat format) {
    uint32_t blocks_x = (w + 3) / 4;
    uint32_t blocks_y = (h + 3) / 4;
    uint32_t blocks_z = (d > 1) ? ((d + 3) / 4) : 1;
    return static_cast<uint64_t>(blocks_x) * blocks_y * blocks_z * GetBytesPerBlock(format);
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

    // Рассчитываем смещения и размеры для каждого мип-уровня
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

    // Обновляем служебные поля заголовка перед записью
    TextureHeader final_header = header;
    final_header.mip_table_offset = mip_table_offset;
    final_header.mip_table_size = mip_table_size;

    // 1. Заголовок
    std::memcpy(write_ptr, &final_header, sizeof(TextureHeader));
    write_ptr += sizeof(TextureHeader);

    // 2. Таблица мипов
    std::memcpy(write_ptr, mip_table.data(), mip_table_size);
    write_ptr += mip_table_size;

    // 3. Сами данные (сжатые мипы)
    std::memcpy(write_ptr, full_data.data(), full_data.size());

    return file_buffer;
}

Result<UnpackedTextureView> TextureBinary::UnpackFull(eastl::span<const uint8_t> raw_bytes) {
    if (raw_bytes.size() < sizeof(TextureHeader)) {
        return tryengine::core::Error("Raw bytes too small for TextureHeader");
    }

    UnpackedTextureView view;
    std::memcpy(&view.header, raw_bytes.data(), sizeof(TextureHeader));

    if (view.header.magic != 0x00584554)
        return LogAndMakeError("Invalid magic number");

    if (view.header.version != 1)
        return LogAndMakeError("Unsupported version");


    uint64_t mip_table_offset = view.header.mip_table_offset;
    uint64_t mip_table_size = view.header.mip_table_size;

    if (raw_bytes.size() < mip_table_offset + mip_table_size)
        return LogAndMakeError("Raw bytes truncated at MipTable");


    uint32_t mip_count = view.header.mip_count;
    view.mips.reserve(mip_count);

    // 1. Читаем таблицу оффсетов из файла
    const MipInfo* file_mip_table = reinterpret_cast<const MipInfo*>(
        raw_bytes.data() + mip_table_offset
    );

    // 2. Инициализируем логические размеры для нарезки
    uint32_t curr_w = view.header.width;
    uint32_t curr_h = view.header.height;
    uint32_t curr_d = view.header.depth;

    // 3. Формируем массив MipView
    for (uint32_t i = 0; i < mip_count; ++i) {
        const MipInfo& info = file_mip_table[i];

        // Строгая валидация: не выходит ли указатель за пределы буфера
        if (info.offset + info.size > raw_bytes.size())
            return LogAndMakeError("Mip data out of bounds");


        MipView mip;
        mip.width = curr_w;
        mip.height = curr_h;
        mip.depth = curr_d;
        mip.size = info.size;

        // Span создается за O(1), просто сохраняя указатель и размер
        mip.data = eastl::span<const uint8_t>(raw_bytes.data() + info.offset, info.size);

        view.mips.push_back(mip);

        // Переход к размерам следующего уровня
        curr_w = (curr_w > 1) ? (curr_w >> 1) : 1;
        curr_h = (curr_h > 1) ? (curr_h >> 1) : 1;
        curr_d = (curr_d > 1) ? (curr_d >> 1) : 1;
    }

    return view;
}

} // namespace tryengine::resources