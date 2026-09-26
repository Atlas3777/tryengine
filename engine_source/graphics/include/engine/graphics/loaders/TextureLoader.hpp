#pragma once

#include <vk_mem_alloc.h>
#include <volk.h>

#include <EASTL/span.h>
#include <EASTL/vector.h>

#include "engine/async/GlobalExecutors.hpp"
#include "engine/core/Assert.hpp"
#include "engine/core/Log.hpp"
#include "engine/core/MakeError.hpp"
#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/graphics/VulkanDevice.hpp"
#include "engine/graphics/VulkanTypesConverter.hpp"
#include "engine/resources/TextureBinary.hpp"
#include "engine/resources/TextureTypes.hpp"

namespace tryengine::graphics {

class TextureLoader {
public:
explicit TextureLoader(VulkanDevice& vulkan_device) : vulkan_device_(vulkan_device) {}

async::Task<TextureSampler> Parse(eastl::span<const uint8_t> data) const {
    auto texture_result = resources::TextureBinary::UnpackFull(data);

    if (!texture_result.has_value())
        co_return LogAndMakeError("Texture unpack failed: {}", texture_result.error().Message());

    auto& [header, mips] = *texture_result;

    if (mips.empty())
        co_return LogAndMakeError("Texture has no mip levels");

    co_await async::ExecutorSwitch(async::MainThread());

    VkDevice device = vulkan_device_.GetDevice();
    VmaAllocator allocator = vulkan_device_.GetAllocator();

    TextureSampler gpu_texture{};
    gpu_texture.allocator_cache = allocator;
    gpu_texture.device_cache = device;

    // 1. Создание Vulkan Image
    VkImageCreateInfo image_info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image_info.imageType = TextureTypeTo(header.type);
    image_info.format = TextureFormatTo(header.format);
    image_info.extent = VkExtent3D{header.width, header.height, (header.type == resources::TextureType::VK_IMAGE_TYPE_3D) ? header.depth : 1u};
    image_info.mipLevels = header.mip_count;
    image_info.arrayLayers = (header.type == resources::TextureType::VK_IMAGE_TYPE_3D) ? 1u : header.array_size;
    image_info.samples = SampleCountTo(header.sample_count);
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (header.view_type == resources::TextureViewType::VK_IMAGE_VIEW_TYPE_CUBE ||
        header.view_type == resources::TextureViewType::VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) {
        image_info.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    }

    VmaAllocationCreateInfo image_alloc_info{};
    image_alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    // ИСПРАВЛЕНИЕ: сохраняем аллокацию напрямую в структуру gpu_texture.image_allocation
    VkResult vk_res = vmaCreateImage(allocator, &image_info, &image_alloc_info, &gpu_texture.image, &gpu_texture.image_allocation, nullptr);
    if (vk_res != VK_SUCCESS)
        co_return LogAndMakeError("Failed to create GPU texture image (VkResult = {})", static_cast<int>(vk_res));

    TRY_ASSERT(gpu_texture.image != VK_NULL_HANDLE && gpu_texture.image_allocation != VK_NULL_HANDLE, "Texture image or allocation handle is null after creation!");

    // 2. Расчет общего размера Mip-уровней и создание Staging Buffer
    VkDeviceSize total_transfer_size = 0;
    for (const auto& mip : mips) {
        total_transfer_size += mip.size;
    }

    VkBufferCreateInfo staging_buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    staging_buffer_info.size = total_transfer_size;
    staging_buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    staging_buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo staging_alloc_info{};
    staging_alloc_info.usage = VMA_MEMORY_USAGE_AUTO;
    staging_alloc_info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VkBuffer staging_buffer = VK_NULL_HANDLE;
    VmaAllocation staging_allocation = VK_NULL_HANDLE;
    VmaAllocationInfo mapped_info{};

    vk_res = vmaCreateBuffer(allocator, &staging_buffer_info, &staging_alloc_info, &staging_buffer, &staging_allocation, &mapped_info);
    if (vk_res != VK_SUCCESS) {
        gpu_texture.Destroy();
        co_return LogAndMakeError("Failed to create staging buffer for texture (VkResult = {})", static_cast<int>(vk_res));
    }

    // 3. Заполнение Staging-буфера
    auto* mapped_bytes = static_cast<uint8_t*>(mapped_info.pMappedData);
    VkDeviceSize current_offset = 0;
    for (const auto& mip : mips) {
        std::memcpy(mapped_bytes + current_offset, mip.data.data(), mip.size);
        current_offset += mip.size;
    }

    // 4. Загрузка данных на GPU
    VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    pool_info.queueFamilyIndex = vulkan_device_.GetGraphicsQueueFamily();

    VkCommandPool cmd_pool = VK_NULL_HANDLE;
    vkCreateCommandPool(device, &pool_info, nullptr, &cmd_pool);

    VkCommandBufferAllocateInfo cmd_alloc_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmd_alloc_info.commandPool = cmd_pool;
    cmd_alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd_alloc_info.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &cmd_alloc_info, &cmd);

    VkCommandBufferBeginInfo begin_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin_info);

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = gpu_texture.image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = header.mip_count;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = image_info.arrayLayers;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    eastl::vector<VkBufferImageCopy> copy_regions;
    copy_regions.reserve(mips.size());

    current_offset = 0;
    for (uint32_t i = 0; i < mips.size(); ++i) {
        const auto& mip = mips[i];

        VkBufferImageCopy region{};
        region.bufferOffset = current_offset;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = i;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = image_info.arrayLayers;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {mip.width, mip.height, mip.depth};

        copy_regions.push_back(region);
        current_offset += mip.size;
    }

    vkCmdCopyBufferToImage(
        cmd,
        staging_buffer,
        gpu_texture.image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(copy_regions.size()),
        copy_regions.data()
    );

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit_info{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd;

    VkQueue graphics_queue = vulkan_device_.GetGraphicsQueue();
    vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphics_queue);

    vkDestroyCommandPool(device, cmd_pool, nullptr);
    vmaDestroyBuffer(allocator, staging_buffer, staging_allocation);

    // 5. Создание VkImageView с использованием header.view_type
    VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view_info.image = gpu_texture.image;
    view_info.viewType = TextureViewTo(header.view_type);
    view_info.format = TextureFormatTo(header.format);
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = header.mip_count;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = image_info.arrayLayers;

    vk_res = vkCreateImageView(device, &view_info, nullptr, &gpu_texture.view);
    if (vk_res != VK_SUCCESS) {
        gpu_texture.Destroy();
        co_return LogAndMakeError("Failed to create VkImageView (VkResult = {})", static_cast<int>(vk_res));
    }

    // 6. Создание VkSampler на основе header.sampler
    const auto& sampler_desc = header.sampler;

    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = FilterTo(sampler_desc.mag_filter);
    sampler_info.minFilter = FilterTo(sampler_desc.min_filter);
    sampler_info.mipmapMode = SamplerMipmapModeTo(sampler_desc.mipmap_mode);
    sampler_info.addressModeU = SamplerAddressModeTo(sampler_desc.address_mode_u);
    sampler_info.addressModeV = SamplerAddressModeTo(sampler_desc.address_mode_v);
    sampler_info.addressModeW = SamplerAddressModeTo(sampler_desc.address_mode_w);
    sampler_info.mipLodBias = sampler_desc.mip_lod_bias;
    sampler_info.anisotropyEnable = sampler_desc.enable_anisotropy ? VK_TRUE : VK_FALSE;
    sampler_info.maxAnisotropy = sampler_desc.max_anisotropy;
    sampler_info.compareEnable = sampler_desc.enable_compare ? VK_TRUE : VK_FALSE;
    sampler_info.compareOp = CompareOpTo(sampler_desc.compare_op);
    sampler_info.minLod = sampler_desc.min_lod;
    sampler_info.maxLod = (sampler_desc.max_lod > 0.0f) ? sampler_desc.max_lod : static_cast<float>(header.mip_count);
    sampler_info.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    sampler_info.unnormalizedCoordinates = VK_FALSE;

    vk_res = vkCreateSampler(device, &sampler_info, nullptr, &gpu_texture.sampler);
    if (vk_res != VK_SUCCESS) {
        gpu_texture.Destroy();
        co_return LogAndMakeError("Failed to create VkSampler (VkResult = {})", static_cast<int>(vk_res));
    }

    co_return eastl::move(gpu_texture);
}


private:
VulkanDevice& vulkan_device_;
};

}  // namespace tryengine::graphics