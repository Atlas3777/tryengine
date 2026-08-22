#include <EASTL/algorithm.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include "editor/BinaryParser.hpp"
#include "editor/import/TextureProcessor.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/resources/TextureBinary.hpp"
#include "rdo_bc_encoder.h"
#include "stb_image.h"
#include "stb_image_resize2.h"

namespace tryeditor {
using tryengine::resources::TextureHeader;
using tryengine::resources::MipInfo;
using tryengine::resources::TextureFormat;

namespace {

bool ResolveCompressionFormat(TextureFormat requested, DXGI_FORMAT& outDxgiFormat, bool& outIsSrgb) {
    switch (requested) {
        case TextureFormat::TEXTUREFORMAT_BC1_RGBA_UNORM:
            outDxgiFormat = DXGI_FORMAT_BC1_UNORM;
            outIsSrgb = false;
            return true;
        case TextureFormat::TEXTUREFORMAT_BC1_RGBA_UNORM_SRGB:
            outDxgiFormat = DXGI_FORMAT_BC1_UNORM_SRGB;
            outIsSrgb = true;
            return true;
        case TextureFormat::TEXTUREFORMAT_BC3_RGBA_UNORM:
            outDxgiFormat = DXGI_FORMAT_BC3_UNORM;
            outIsSrgb = false;
            return true;
        case TextureFormat::TEXTUREFORMAT_BC3_RGBA_UNORM_SRGB:
            outDxgiFormat = DXGI_FORMAT_BC3_UNORM_SRGB;
            outIsSrgb = true;
            return true;
        case TextureFormat::TEXTUREFORMAT_BC4_R_UNORM:
            outDxgiFormat = DXGI_FORMAT_BC4_UNORM;
            outIsSrgb = false;
            return true;
        case TextureFormat::TEXTUREFORMAT_BC5_RG_UNORM:
            outDxgiFormat = DXGI_FORMAT_BC5_UNORM;
            outIsSrgb = false;
            return true;
        case TextureFormat::TEXTUREFORMAT_BC7_RGBA_UNORM:
            outDxgiFormat = DXGI_FORMAT_BC7_UNORM;
            outIsSrgb = false;
            return true;
        case TextureFormat::TEXTUREFORMAT_BC7_RGBA_UNORM_SRGB:
            outDxgiFormat = DXGI_FORMAT_BC7_UNORM_SRGB;
            outIsSrgb = true;
            return true;
        default:
            return false;
    }
}

// Размер одного BC-блока в байтах: 8 для BC1/BC4, 16 для BC2/BC3/BC5/BC6/BC7.
// Раньше размер (16 байт) был жёстко зашит в GetBC7MipSize и был бы неверным
// для любого формата, кроме BC7.
uint32_t GetBytesPerBlock(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_BC1_UNORM:
        case DXGI_FORMAT_BC1_UNORM_SRGB:
        case DXGI_FORMAT_BC4_UNORM:
            return 8;
        default:
            return 16;
    }
}


uint64_t GetCompressedMipSize(uint32_t w, uint32_t h, DXGI_FORMAT format) {
    uint32_t blocks_x = (w + 3) / 4;
    uint32_t blocks_y = (h + 3) / 4;
    return static_cast<uint64_t>(blocks_x) * blocks_y * GetBytesPerBlock(format);
}

tryengine::Result<bool> CompressMipLevel(const uint8_t* rgba, uint32_t w, uint32_t h, DXGI_FORMAT dxgi_format, bool perceptual,
                      uint8_t* outBuffer, uint64_t expected_size) {
    utils::image_u8 src_image(w, h);
    std::memcpy(src_image.get_pixels().data(), rgba, static_cast<size_t>(w) * h * 4);

    rdo_bc::rdo_bc_params params;
    params.m_dxgi_format = dxgi_format;
    params.m_perceptual = perceptual;  // корректная метрика ошибки для sRGB/цветовых данных
    params.m_rdo_multithreading = true;
    params.m_status_output = false;
    params.m_bc7_uber_level = 4;  // компромисс скорость/качество для BC7

    rdo_bc::rdo_bc_encoder encoder;
    if (!encoder.init(src_image, params))
        return tryengine::core::Error("rdo_bc_encoder::init failed");

    if (!encoder.encode())
        return tryengine::core::Error("rdo_bc_encoder::encode failed");

    const uint64_t actualSize = encoder.get_total_blocks_size_in_bytes();
    if (actualSize != expected_size)
        return tryengine::core::Error("compressed mip size mismatch between prediction and encoder output");


    std::memcpy(outBuffer, encoder.get_blocks(), actualSize);
    return true;
}


void GenerateNextMip(const uint8_t* src, uint32_t srcW, uint32_t srcH, uint8_t* dst, uint32_t dstW, uint32_t dstH,
                     bool isSrgb) {
    if (isSrgb) {
        stbir_resize_uint8_srgb(src, static_cast<int>(srcW), static_cast<int>(srcH), 0, dst, static_cast<int>(dstW),
                                static_cast<int>(dstH), 0, STBIR_RGBA);
    } else {
        stbir_resize_uint8_linear(src, static_cast<int>(srcW), static_cast<int>(srcH), 0, dst, static_cast<int>(dstW),
                                  static_cast<int>(dstH), 0, STBIR_RGBA);
    }
}

}  // namespace

tryengine::Result<eastl::vector<uint8_t>> TextureProcessor::ProcessFromEncodedMemory(
    eastl::span<const uint8_t> encoded_bytes, const TextureProcessSettings& settings) {
    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels =
        stbi_load_from_memory(encoded_bytes.data(), static_cast<int>(encoded_bytes.size()), &w, &h, &comp, 4);
    if (!pixels) {
        return tryengine::core::Error("Failed to decode image memory via stb_image");
    }

    auto result = ProcessFromRawPixels(pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h), settings);
    stbi_image_free(pixels);

    return result;
}

tryengine::Result<eastl::vector<uint8_t>> TextureProcessor::ProcessFromRawPixels(
    const uint8_t* rgba_data, uint32_t width, uint32_t height, const TextureProcessSettings& settings) {
    if (!rgba_data || width == 0 || height == 0) {
        return LogAndMakeError("ProcessFromRawPixels: null pixel data or zero-sized image");
    }

    DXGI_FORMAT dxgi_format;
    bool is_srgb = false;
    if (!ResolveCompressionFormat(settings.format, dxgi_format, is_srgb)) {
        return LogAndMakeError("ProcessFromRawPixels: settings.format is not a supported BC compression format");
    }

    // 1. Расчет количества мип-уровней
    uint32_t mip_count = 1;
    {
        uint32_t temp_w = width, temp_h = height;
        while (temp_w > 1 || temp_h > 1) {
            temp_w >>= 1;
            temp_h >>= 1;
            mip_count++;
        }
    }

    // 2. Расчет общего размера сжатых данных
    uint64_t total_compressed_size = 0;
    {
        uint32_t temp_w = width, temp_h = height;
        for (uint32_t i = 0; i < mip_count; ++i) {
            total_compressed_size += GetCompressedMipSize(temp_w, temp_h, dxgi_format);
            temp_w = (temp_w > 1) ? (temp_w >> 1) : 1;
            temp_h = (temp_h > 1) ? (temp_h >> 1) : 1;
        }
    }

    // 3. Аллокация буфера только под "сырые" сжатые мипы
    eastl::vector<uint8_t> compressed_mips(total_compressed_size);
    uint8_t* write_ptr = compressed_mips.data();

    // 4. Генерация мипов и сжатие
    eastl::vector<uint8_t> current_mip_rgba(static_cast<size_t>(width) * height * 4);
    std::memcpy(current_mip_rgba.data(), rgba_data, current_mip_rgba.size());

    uint32_t temp_w = width;
    uint32_t temp_h = height;
    for (uint32_t i = 0; i < mip_count; ++i) {
        uint64_t expected_size = GetCompressedMipSize(temp_w, temp_h, dxgi_format);

        eastl::string encode_error;
        auto res = CompressMipLevel(current_mip_rgba.data(), temp_w, temp_h, dxgi_format, is_srgb, write_ptr, expected_size);
        if (!res.has_value())
            return LogAndMakeError("ProcessFromRawPixels: failed to compress mip {}:{}", i, encode_error);

        write_ptr += expected_size;

        // Генерация следующего уровня
        if (i < mip_count - 1) {
            uint32_t next_w = (temp_w > 1) ? (temp_w >> 1) : 1;
            uint32_t next_h = (temp_h > 1) ? (temp_h >> 1) : 1;
            eastl::vector<uint8_t> next_mip_rgba(static_cast<size_t>(next_w) * next_h * 4);

            GenerateNextMip(current_mip_rgba.data(), temp_w, temp_h, next_mip_rgba.data(), next_w, next_h, is_srgb);

            current_mip_rgba = std::move(next_mip_rgba);
            temp_w = next_w;
            temp_h = next_h;
        }
    }

    // 5. Формирование базового заголовка (без полей оффсетов)
    TextureHeader header;
    header.magic = 0x00584554;
    header.version = 1;
    header.format = settings.format;
    header.type = tryengine::resources::TextureType::TEXTURETYPE_2D;
    header.width = width;
    header.height = height;
    header.depth = 1;
    header.array_size = 1;
    header.face_count = 1;
    header.mip_count = mip_count;
    header.msaa_count = static_cast<uint32_t>(tryengine::resources::MSAACount::SDL_GPU_SAMPLECOUNT_1);
    header.flags = 0;
    // mip_table_offset и mip_table_size будут рассчитаны внутри TextureBinary::Pack

    // 6. Передача ответственности за упаковку в TextureBinary
    return tryengine::resources::TextureBinary::Pack(header, compressed_mips);
}

}  // namespace tryeditor