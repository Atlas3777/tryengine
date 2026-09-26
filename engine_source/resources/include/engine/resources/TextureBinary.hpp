#pragma once

#include <EASTL/span.h>
#include <EASTL/vector.h>

#include "engine/core/Result.hpp"
#include "engine/resources/TextureTypes.hpp"

namespace tryengine::resources {

constexpr uint32_t TextureMagic = 0x00584554;
constexpr uint32_t TextureVersion = 2;

#pragma pack(push, 1)

struct SamplerDesc {
    VkFilter min_filter{VkFilter::VK_FILTER_LINEAR};
    VkFilter mag_filter{VkFilter::VK_FILTER_LINEAR};
    VkSamplerMipmapMode mipmap_mode{VkSamplerMipmapMode::VK_SAMPLER_MIPMAP_MODE_LINEAR};
    VkSamplerAddressMode address_mode_u{VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT};
    VkSamplerAddressMode address_mode_v{VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT};
    VkSamplerAddressMode address_mode_w{VkSamplerAddressMode::VK_SAMPLER_ADDRESS_MODE_REPEAT};
    VkCompareOp compare_op{VkCompareOp::VK_COMPARE_OP_NEVER};
    bool enable_anisotropy{false};
    bool enable_compare{false};

    float mip_lod_bias{0.0f};
    float max_anisotropy{1.0f};
    float min_lod{0.0f};
    float max_lod{0.0f};
};

struct TextureHeader {
    uint32_t magic{TextureMagic};
    uint32_t version{TextureVersion};

    TextureFormat format{TextureFormat::VK_FORMAT_UNDEFINED};
    TextureType type{TextureType::VK_IMAGE_TYPE_2D};
    TextureViewType view_type{TextureViewType::VK_IMAGE_VIEW_TYPE_2D};

    uint32_t width{0};
    uint32_t height{0};
    uint32_t depth{1};

    uint32_t array_size{1};
    uint32_t face_count{1};
    uint32_t mip_count{1};

    TextureSampleCount sample_count{TextureSampleCount::VK_SAMPLE_COUNT_1_BIT};
    uint32_t flags{0};

    SamplerDesc sampler{};

    uint64_t mip_table_offset{0};
    uint64_t mip_table_size{0};
};

#pragma pack(pop)

struct MipInfo {
    uint64_t offset;
    uint64_t size;
};

struct MipView {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint64_t size;
    eastl::span<const uint8_t> data;
};

struct UnpackedTextureView {
    TextureHeader header;
    eastl::vector<MipView> mips;
};

class TextureBinary {
public:
    static eastl::vector<uint8_t> Pack(const TextureHeader& header,
                                       eastl::span<const uint8_t> full_data);

    static Result<UnpackedTextureView> UnpackFull(eastl::span<const uint8_t> raw_bytes);
};

}  // namespace tryengine::resources