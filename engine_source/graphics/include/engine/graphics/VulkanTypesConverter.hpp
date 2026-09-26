#pragma once

#include <vulkan/vulkan_core.h>

#include "engine/resources/TextureTypes.hpp"

namespace tryengine::graphics {

constexpr VkFormat TextureFormatTo(resources::TextureFormat format) {
    return static_cast<VkFormat>(format);
}

constexpr VkImageType TextureTypeTo(resources::TextureType type) {
    return static_cast<VkImageType>(type);
}

constexpr VkImageViewType TextureViewTo(resources::TextureViewType type) {
    return static_cast<VkImageViewType>(type);
}

constexpr VkFilter FilterTo(resources::VkFilter filter) {
    if (filter == resources::VkFilter::VK_FILTER_CUBIC_EXT) {
        return VK_FILTER_CUBIC_EXT;
    }
    return static_cast<VkFilter>(filter);
}

constexpr VkSamplerAddressMode SamplerAddressModeTo(resources::VkSamplerAddressMode mode) {
    return static_cast<VkSamplerAddressMode>(mode);
}

constexpr VkSamplerMipmapMode SamplerMipmapModeTo(resources::VkSamplerMipmapMode mode) {
    return static_cast<VkSamplerMipmapMode>(mode);
}

constexpr VkCompareOp CompareOpTo(resources::VkCompareOp op) {
    return static_cast<VkCompareOp>(op);
}

constexpr VkSampleCountFlagBits SampleCountTo(resources::TextureSampleCount count) {
    return static_cast<VkSampleCountFlagBits>(count);
}

}  // namespace tryengine::graphics