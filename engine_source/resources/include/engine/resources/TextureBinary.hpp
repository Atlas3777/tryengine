#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>

#include "engine/resources/TextureTypes.hpp"
#include "engine/core/Result.hpp"

namespace tryengine::resources {

#pragma pack(push, 1)
struct TextureHeader {
    uint32_t magic;    // 0x00584554 ("TEX\0")
    uint32_t version;  // 1

    TextureFormat format;
    TextureType type;

    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t array_size;
    uint32_t face_count;
    uint32_t mip_count;

    uint32_t msaa_count;
    uint32_t flags; // unused?

    uint64_t mip_table_offset;
    uint64_t mip_table_size;
};
#pragma pack(pop)

static_assert(sizeof(TextureHeader) == 64, "Header must be exactly 64 bytes");


struct MipInfo {
    uint64_t offset;
    uint64_t size;
};


struct Sampler {
    TextureFilter min_filter;          /**< The minification filter to apply to lookups. */
    TextureFilter mag_filter;          /**< The magnification filter to apply to lookups. */
    SamplerMipmapMode mipmap_mode;     /**< The mipmap filter to apply to lookups. */
    SamplerAddressMode address_mode_u; /**< The addressing mode for U coordinates outside [0,1). */
    SamplerAddressMode address_mode_v; /**< The addressing mode for V coordinates outside [0, 1). */
    SamplerAddressMode address_mode_w; /**< The addressing mode for W coordinates outside [0, 1). */
    float mip_lod_bias;                /**< The bias to be added to mipmap LOD calculation. */
    float max_anisotropy;   /**< The anisotropy value clamp used by the sampler. If enable_anisotropy is false, this is
                               ignored. */
    CompareOp compare_op;   /**< The comparison operator to apply to fetched data before filtering. */
    float min_lod;          /**< Clamps the minimum of the computed LOD value. */
    float max_lod;          /**< Clamps the maximum of the computed LOD value. */
    bool enable_anisotropy; /**< true to enable anisotropic filtering. */
    bool enable_compare;    /**< true to enable comparison against a reference value during lookups. */
};


struct MipView {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint64_t size; // Размер в байтах (с учетом padding для row pitch)
    eastl::span<const uint8_t> data; // Zero-copy ссылка на исходный буфер
};


struct UnpackedTextureView {
    TextureHeader header;
    eastl::vector<MipView> mips; // Готовый массив для передачи в GPU API
};


class TextureBinary {
public:
    static eastl::vector<uint8_t> Pack(const TextureHeader& header,
                                       eastl::span<const uint8_t> full_data);

    static Result<UnpackedTextureView> UnpackFull(eastl::span<const uint8_t> raw_bytes);
};

}  // namespace tryengine::resources