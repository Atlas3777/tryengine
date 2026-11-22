#include <EASTL/algorithm.h>
#include <EASTL/string.h>
#include <EASTL/vector.h>

#include "editor/BinaryParser.hpp"
#include "editor/import/TextureProcessor.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/resources/binary/TextureBinary.hpp"
#include "rdo_bc_encoder.h"
#include "stb_image.h"
#include "stb_image_resize2.h"

namespace tryeditor {
using tryengine::resources::MipInfo;
using tryengine::resources::TextureFormat;
using tryengine::resources::TextureHeader;

namespace {

bool ResolveCompressionFormat(TextureFormat requested, DXGI_FORMAT& outDxgiFormat, bool& outIsSrgb) {
    switch (requested) {
        // BC1 (DXT1)
        case TextureFormat::VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
            outDxgiFormat = DXGI_FORMAT_BC1_UNORM;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case TextureFormat::VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
            outDxgiFormat = DXGI_FORMAT_BC1_UNORM_SRGB;
            outIsSrgb = true;
            return true;

        // BC2 (DXT3)
        case TextureFormat::VK_FORMAT_BC2_UNORM_BLOCK:
            outDxgiFormat = DXGI_FORMAT_BC2_UNORM;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_BC2_SRGB_BLOCK:
            outDxgiFormat = DXGI_FORMAT_BC2_UNORM_SRGB;
            outIsSrgb = true;
            return true;

        // BC3 (DXT5)
        case TextureFormat::VK_FORMAT_BC3_UNORM_BLOCK:
            outDxgiFormat = DXGI_FORMAT_BC3_UNORM;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_BC3_SRGB_BLOCK:
            outDxgiFormat = DXGI_FORMAT_BC3_UNORM_SRGB;
            outIsSrgb = true;
            return true;

        // BC4 (ATI1 / Red Channel)
        case TextureFormat::VK_FORMAT_BC4_UNORM_BLOCK:
            outDxgiFormat = DXGI_FORMAT_BC4_UNORM;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_BC4_SNORM_BLOCK:
            outDxgiFormat = DXGI_FORMAT_BC4_SNORM;
            outIsSrgb = false;
            return true;

        // BC5 (ATI2 / RGTC2 / Normal Maps)
        case TextureFormat::VK_FORMAT_BC5_UNORM_BLOCK:
            outDxgiFormat = DXGI_FORMAT_BC5_UNORM;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_BC5_SNORM_BLOCK:
            outDxgiFormat = DXGI_FORMAT_BC5_SNORM;
            outIsSrgb = false;
            return true;

        // BC6H (HDR)
        case TextureFormat::VK_FORMAT_BC6H_UFLOAT_BLOCK:
            outDxgiFormat = DXGI_FORMAT_BC6H_UF16;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_BC6H_SFLOAT_BLOCK:
            outDxgiFormat = DXGI_FORMAT_BC6H_SF16;
            outIsSrgb = false;
            return true;

        // BC7 (High Quality RGBA)
        case TextureFormat::VK_FORMAT_BC7_UNORM_BLOCK:
            outDxgiFormat = DXGI_FORMAT_BC7_UNORM;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_BC7_SRGB_BLOCK:
            outDxgiFormat = DXGI_FORMAT_BC7_UNORM_SRGB;
            outIsSrgb = true;
            return true;

        // Standard 8-bit Uncompressed Formats
        case TextureFormat::VK_FORMAT_R8_UNORM:
            outDxgiFormat = DXGI_FORMAT_R8_UNORM;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_R8G8_UNORM:
            outDxgiFormat = DXGI_FORMAT_R8G8_UNORM;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_R8G8B8A8_UNORM:
            outDxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_R8G8B8A8_SRGB:
            outDxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
            outIsSrgb = true;
            return true;
        case TextureFormat::VK_FORMAT_B8G8R8A8_UNORM:
            outDxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_B8G8R8A8_SRGB:
            outDxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
            outIsSrgb = true;
            return true;

        // 16-bit na 32-bit Float Formats
        case TextureFormat::VK_FORMAT_R16_SFLOAT:
            outDxgiFormat = DXGI_FORMAT_R16_FLOAT;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_R16G16_SFLOAT:
            outDxgiFormat = DXGI_FORMAT_R16G16_FLOAT;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_R16G16B16A16_SFLOAT:
            outDxgiFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_R32_SFLOAT:
            outDxgiFormat = DXGI_FORMAT_R32_FLOAT;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_R32G32_SFLOAT:
            outDxgiFormat = DXGI_FORMAT_R32G32_FLOAT;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_R32G32B32_SFLOAT:
            outDxgiFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_R32G32B32A32_SFLOAT:
            outDxgiFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
            outIsSrgb = false;
            return true;

        // Depth / Stencil Formats
        case TextureFormat::VK_FORMAT_D16_UNORM:
            outDxgiFormat = DXGI_FORMAT_D16_UNORM;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_D24_UNORM_S8_UINT:
            outDxgiFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
            outIsSrgb = false;
            return true;
        case TextureFormat::VK_FORMAT_D32_SFLOAT:
            outDxgiFormat = DXGI_FORMAT_D32_FLOAT;
            outIsSrgb = false;
            return true;

        default:
            outDxgiFormat = DXGI_FORMAT_UNKNOWN;
            outIsSrgb = false;
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

constexpr DXGI_FORMAT ToLinearDXGIFormat(DXGI_FORMAT format) noexcept {
    switch (format) {
        // Compressed Block Formats
        case DXGI_FORMAT_BC1_UNORM_SRGB:
            return DXGI_FORMAT_BC1_UNORM;
        case DXGI_FORMAT_BC2_UNORM_SRGB:
            return DXGI_FORMAT_BC2_UNORM;
        case DXGI_FORMAT_BC3_UNORM_SRGB:
            return DXGI_FORMAT_BC3_UNORM;
        case DXGI_FORMAT_BC7_UNORM_SRGB:
            return DXGI_FORMAT_BC7_UNORM;

            // Standard RGBA Formats (на случай поддержки несжатых текстур)
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            return DXGI_FORMAT_R32G32B32A32_FLOAT;

        default:
            return format;
    }
}

tryengine::Result<bool> CompressMipLevel(const uint8_t* rgba, uint32_t w, uint32_t h, DXGI_FORMAT dxgi_format,
                                         bool perceptual, uint8_t* outBuffer, uint64_t expected_size) {
    utils::image_u8 src_image(w, h);
    std::memcpy(src_image.get_pixels().data(), rgba, static_cast<size_t>(w) * h * 4);

    rdo_bc::rdo_bc_params params;
    params.m_dxgi_format = dxgi_format;
    params.m_perceptual = perceptual;
    params.m_rdo_multithreading = true;
    params.m_status_output = false;
    params.m_bc7_uber_level = 4;  // компромисс скорость/качество для BC7

    params.m_dxgi_format = ToLinearDXGIFormat(dxgi_format);

    rdo_bc::rdo_bc_encoder encoder;
    if (!encoder.init(src_image, params))
        return LogAndMakeError("rdo_bc_encoder::init failed (dxgi={}, {}x{})", static_cast<int>(dxgi_format), w, h);

    if (!encoder.encode())
        return LogAndMakeError("rdo_bc_encoder::encode failed (dxgi={}, {}x{})", static_cast<int>(dxgi_format), w, h);
    const uint64_t actualSize = encoder.get_total_blocks_size_in_bytes();

    if (actualSize != expected_size)
        return LogAndMakeError("mip size mismatch: predicted {}, encoder {}", expected_size, actualSize);

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
    if (!pixels)
        return tryengine::core::Error("Failed to decode image memory via stb_image");

    auto result = ProcessFromRawPixels(pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h), settings);
    stbi_image_free(pixels);

    return result;
}

namespace {

bool IsBcFormat(TextureFormat format) {
    switch (format) {
        case TextureFormat::VK_FORMAT_BC1_RGB_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case TextureFormat::VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case TextureFormat::VK_FORMAT_BC2_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC2_SRGB_BLOCK:
        case TextureFormat::VK_FORMAT_BC3_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC3_SRGB_BLOCK:
        case TextureFormat::VK_FORMAT_BC4_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC4_SNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC5_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC5_SNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC6H_UFLOAT_BLOCK:
        case TextureFormat::VK_FORMAT_BC6H_SFLOAT_BLOCK:
        case TextureFormat::VK_FORMAT_BC7_UNORM_BLOCK:
        case TextureFormat::VK_FORMAT_BC7_SRGB_BLOCK:
            return true;
        default:
            return false;
    }
}

bool IsSrgbFormat(TextureFormat format) {
    switch (format) {
        case TextureFormat::VK_FORMAT_R8_SRGB:
        case TextureFormat::VK_FORMAT_R8G8_SRGB:
        case TextureFormat::VK_FORMAT_R8G8B8A8_SRGB:
        case TextureFormat::VK_FORMAT_B8G8R8A8_SRGB:
        case TextureFormat::VK_FORMAT_BC1_RGB_SRGB_BLOCK:
        case TextureFormat::VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
        case TextureFormat::VK_FORMAT_BC2_SRGB_BLOCK:
        case TextureFormat::VK_FORMAT_BC3_SRGB_BLOCK:
        case TextureFormat::VK_FORMAT_BC7_SRGB_BLOCK:
            return true;
        default:
            return false;
    }
}

// Bytes per pixel of an uncompressed 8-bit format stb_image's RGBA can be written to; 0 means
// the format is not one we produce.
uint32_t UncompressedBytesPerPixel(TextureFormat format) {
    switch (format) {
        case TextureFormat::VK_FORMAT_R8_UNORM:
        case TextureFormat::VK_FORMAT_R8_SRGB:
            return 1;
        case TextureFormat::VK_FORMAT_R8G8_UNORM:
        case TextureFormat::VK_FORMAT_R8G8_SRGB:
            return 2;
        case TextureFormat::VK_FORMAT_R8G8B8A8_UNORM:
        case TextureFormat::VK_FORMAT_R8G8B8A8_SRGB:
        case TextureFormat::VK_FORMAT_B8G8R8A8_UNORM:
        case TextureFormat::VK_FORMAT_B8G8R8A8_SRGB:
        case TextureFormat::VK_FORMAT_A8B8G8R8_UNORM_PACK32:
            return 4;
        default:
            return 0;
    }
}

uint32_t MipCountFor(uint32_t width, uint32_t height) {
    uint32_t count = 1;
    uint32_t w = width;
    uint32_t h = height;
    while (w > 1 || h > 1) {
        w = (w > 1) ? (w >> 1) : 1;
        h = (h > 1) ? (h >> 1) : 1;
        count++;
    }
    return count;
}

// RGBA bytes -> one uncompressed mip in the target layout. Only B8G8R8A8 reorders channels;
// R8 and R8G8 drop the trailing ones.
void WriteUncompressedMip(const uint8_t* rgba, uint32_t width, uint32_t height, TextureFormat format, uint8_t* out) {
    const uint32_t bpp = UncompressedBytesPerPixel(format);
    const bool swap_red_blue =
        format == TextureFormat::VK_FORMAT_B8G8R8A8_UNORM || format == TextureFormat::VK_FORMAT_B8G8R8A8_SRGB;
    const uint32_t pixels = width * height;
    for (uint32_t i = 0; i < pixels; ++i) {
        const uint8_t* px = rgba + static_cast<size_t>(i) * 4;
        switch (bpp) {
            case 1:
                out[i] = px[0];
                break;
            case 2:
                out[i * 2 + 0] = px[0];
                out[i * 2 + 1] = px[1];
                break;
            default:
                if (swap_red_blue) {
                    out[i * 4 + 0] = px[2];
                    out[i * 4 + 1] = px[1];
                    out[i * 4 + 2] = px[0];
                    out[i * 4 + 3] = px[3];
                } else {
                    std::memcpy(out + static_cast<size_t>(i) * 4, px, 4);
                }
                break;
        }
    }
}

}  // namespace

tryengine::Result<eastl::vector<uint8_t>> TextureProcessor::ProcessFromRawPixels(
    const uint8_t* rgba_data, uint32_t width, uint32_t height, const TextureProcessSettings& settings) {
    if (!rgba_data || width == 0 || height == 0)
        return LogAndMakeError("ProcessFromRawPixels: null pixel data or zero-sized image");

    const bool compressed = IsBcFormat(settings.format);
    const bool is_srgb = IsSrgbFormat(settings.format);
    const uint32_t bpp = UncompressedBytesPerPixel(settings.format);
    if (!compressed && bpp == 0)
        return LogAndMakeError("ProcessFromRawPixels: texture format {} is not supported by the processor",
                               static_cast<int>(settings.format));

    // 0. Optional downscale: max_size clamps the longest side and keeps the aspect. It happens
    //    before mips so every level comes from the final base image.
    eastl::vector<uint8_t> scaled;
    const uint8_t* base = rgba_data;
    uint32_t w = width;
    uint32_t h = height;
    if (settings.max_size > 0 && (width > settings.max_size || height > settings.max_size)) {
        const float scale = static_cast<float>(settings.max_size) / static_cast<float>(eastl::max(width, height));
        const uint32_t nw = eastl::max(1u, static_cast<uint32_t>(static_cast<float>(width) * scale));
        const uint32_t nh = eastl::max(1u, static_cast<uint32_t>(static_cast<float>(height) * scale));
        scaled.resize(static_cast<size_t>(nw) * nh * 4);
        GenerateNextMip(rgba_data, width, height, scaled.data(), nw, nh, is_srgb);
        base = scaled.data();
        w = nw;
        h = nh;
    }

    const uint32_t mip_count = settings.generate_mips ? MipCountFor(w, h) : 1;

    // 1. Header is the same for both paths; TextureBinary::Pack fills in the mip table.
    TextureHeader header;
    header.magic = tryengine::resources::TextureMagic;
    header.version = tryengine::resources::TextureVersion;
    header.format = settings.format;
    header.type = tryengine::resources::TextureType::VK_IMAGE_TYPE_2D;
    header.width = w;
    header.height = h;
    header.depth = 1;
    header.array_size = 1;
    header.face_count = 1;
    header.mip_count = mip_count;
    header.sample_count = tryengine::resources::TextureSampleCount::VK_SAMPLE_COUNT_1_BIT;
    header.flags = 0;
    header.sampler = settings.sampler;

    // 2. Compressed path: rdo_bc encodes every mip.
    if (compressed) {
        DXGI_FORMAT dxgi_format;
        bool dxgi_srgb = false;
        if (!ResolveCompressionFormat(settings.format, dxgi_format, dxgi_srgb))
            return LogAndMakeError("ProcessFromRawPixels: settings.format is not a supported BC compression format");

        uint64_t total_compressed_size = 0;
        {
            uint32_t temp_w = w, temp_h = h;
            for (uint32_t i = 0; i < mip_count; ++i) {
                total_compressed_size += GetCompressedMipSize(temp_w, temp_h, dxgi_format);
                temp_w = (temp_w > 1) ? (temp_w >> 1) : 1;
                temp_h = (temp_h > 1) ? (temp_h >> 1) : 1;
            }
        }

        eastl::vector<uint8_t> compressed_mips(total_compressed_size);
        uint8_t* write_ptr = compressed_mips.data();

        eastl::vector<uint8_t> current_mip_rgba(static_cast<size_t>(w) * h * 4);
        std::memcpy(current_mip_rgba.data(), base, current_mip_rgba.size());

        uint32_t temp_w = w;
        uint32_t temp_h = h;
        for (uint32_t i = 0; i < mip_count; ++i) {
            const uint64_t expected_size = GetCompressedMipSize(temp_w, temp_h, dxgi_format);

            auto res = CompressMipLevel(current_mip_rgba.data(), temp_w, temp_h, dxgi_format, dxgi_srgb, write_ptr,
                                        expected_size);
            if (!res.has_value())
                return LogAndMakeError("ProcessFromRawPixels: failed to compress mip {}", i);
            write_ptr += expected_size;

            if (i < mip_count - 1) {
                const uint32_t next_w = (temp_w > 1) ? (temp_w >> 1) : 1;
                const uint32_t next_h = (temp_h > 1) ? (temp_h >> 1) : 1;
                eastl::vector<uint8_t> next_mip_rgba(static_cast<size_t>(next_w) * next_h * 4);
                GenerateNextMip(current_mip_rgba.data(), temp_w, temp_h, next_mip_rgba.data(), next_w, next_h,
                                dxgi_srgb);
                current_mip_rgba = std::move(next_mip_rgba);
                temp_w = next_w;
                temp_h = next_h;
            }
        }

        return tryengine::resources::TextureBinary::Pack(header, compressed_mips);
    }

    // 3. Uncompressed path: resize each level and write the format's bytes.
    uint64_t total_size = 0;
    {
        uint32_t temp_w = w, temp_h = h;
        for (uint32_t i = 0; i < mip_count; ++i) {
            total_size += static_cast<uint64_t>(temp_w) * temp_h * bpp;
            temp_w = (temp_w > 1) ? (temp_w >> 1) : 1;
            temp_h = (temp_h > 1) ? (temp_h >> 1) : 1;
        }
    }

    eastl::vector<uint8_t> mips(total_size);
    uint8_t* write_ptr = mips.data();

    eastl::vector<uint8_t> current_mip_rgba(static_cast<size_t>(w) * h * 4);
    std::memcpy(current_mip_rgba.data(), base, current_mip_rgba.size());

    uint32_t temp_w = w;
    uint32_t temp_h = h;
    for (uint32_t i = 0; i < mip_count; ++i) {
        WriteUncompressedMip(current_mip_rgba.data(), temp_w, temp_h, settings.format, write_ptr);
        write_ptr += static_cast<uint64_t>(temp_w) * temp_h * bpp;

        if (i < mip_count - 1) {
            const uint32_t next_w = (temp_w > 1) ? (temp_w >> 1) : 1;
            const uint32_t next_h = (temp_h > 1) ? (temp_h >> 1) : 1;
            eastl::vector<uint8_t> next_mip_rgba(static_cast<size_t>(next_w) * next_h * 4);
            GenerateNextMip(current_mip_rgba.data(), temp_w, temp_h, next_mip_rgba.data(), next_w, next_h, is_srgb);
            current_mip_rgba = std::move(next_mip_rgba);
            temp_w = next_w;
            temp_h = next_h;
        }
    }

    return tryengine::resources::TextureBinary::Pack(header, mips);
}

}  // namespace tryeditor
