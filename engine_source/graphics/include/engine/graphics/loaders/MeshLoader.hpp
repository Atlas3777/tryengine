#pragma once

#include "engine/async/GlobalExecutors.hpp"
#include "engine/graphics/RuntimeTypes.hpp"
#include "engine/graphics/VulkanDevice.hpp"
#include "engine/resources/MeshBinary.hpp"

#include <EASTL/span.h>
#include <vk_mem_alloc.h>
#include <volk.h>
#include <cstring>

namespace tryengine::graphics {

__forceinline VkIndexType IndexFormat(const resources::IndexFormat format) {
    if (format == resources::IndexFormat::UInt16)
        return VK_INDEX_TYPE_UINT16;
    if (format == resources::IndexFormat::UInt32)
        return VK_INDEX_TYPE_UINT32;
    if (format == resources::IndexFormat::UInt8)
        return VK_INDEX_TYPE_UINT8;
    TRY_ASSERT(false, "Invalid index format");
    return VK_INDEX_TYPE_NONE_KHR;
}

class MeshLoader {
public:
    explicit MeshLoader(VulkanDevice& device) : device_(device) {}

    async::Task<Mesh> Parse(eastl::span<const uint8_t> raw_bytes) {
        auto unpack_res = resources::MeshBinary::Unpack(raw_bytes);
        if (!unpack_res.has_value()) {
            co_return LogAndMakeError("Failed to load mesh: {}", unpack_res.error().Message());
        }

        const auto& [header, position_data, attribute_data, index_data] = *unpack_res;

        Mesh mesh{};
        mesh.allocator_cache = device_.GetAllocator();
        mesh.attribute_flags = header.attribute_flags;
        mesh.attribute_stride = header.attribute_stride;
        mesh.index_type = IndexFormat(header.index_format);
        mesh.num_indices = header.index_count;

        co_await async::ExecutorSwitch{async::MainThread()};

        VkDevice vk_device = device_.GetDevice();
        VmaAllocator allocator = device_.GetAllocator();

        auto get_buffer_address = [vk_device](VkBuffer buffer) -> VkDeviceAddress {
            VkBufferDeviceAddressInfo info{
                .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
                .buffer = buffer,
            };
            return vkGetBufferDeviceAddress(vk_device, &info);
        };

        VmaAllocationCreateInfo gpu_alloc_info{
            .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
        };

        // 1. Позиции (Position Buffer)
        VkBufferCreateInfo pos_buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = position_data.size_bytes(),
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        if (vmaCreateBuffer(allocator, &pos_buffer_info, &gpu_alloc_info, &mesh.position_buffer, &mesh.position_allocation, nullptr) != VK_SUCCESS) {
            co_return LogAndMakeError("Failed to create position buffer");
        }
        mesh.position_buffer_addr = get_buffer_address(mesh.position_buffer);

        // 2. Атрибуты (Attribute Buffer)
        VkBufferCreateInfo attr_buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = attribute_data.size_bytes(),
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        if (vmaCreateBuffer(allocator, &attr_buffer_info, &gpu_alloc_info, &mesh.attribute_buffer, &mesh.attribute_allocation, nullptr) != VK_SUCCESS) {
            vmaDestroyBuffer(allocator, mesh.position_buffer, mesh.position_allocation);
            co_return LogAndMakeError("Failed to create attribute buffer");
        }
        mesh.attribute_buffer_addr = get_buffer_address(mesh.attribute_buffer);

        // 3. Индексы (Index Buffer)
        VkBufferCreateInfo index_buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = index_data.size_bytes(),
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        if (vmaCreateBuffer(allocator, &index_buffer_info, &gpu_alloc_info, &mesh.index_buffer, &mesh.index_allocation, nullptr) != VK_SUCCESS) {
            vmaDestroyBuffer(allocator, mesh.position_buffer, mesh.position_allocation);
            vmaDestroyBuffer(allocator, mesh.attribute_buffer, mesh.attribute_allocation);
            co_return LogAndMakeError("Failed to create index buffer");
        }
        mesh.index_buffer_addr = get_buffer_address(mesh.index_buffer);

        // 4. Staging-буфер для загрузки CPU -> GPU
        const VkDeviceSize pos_size = position_data.size_bytes();
        const VkDeviceSize attr_size = attribute_data.size_bytes();
        const VkDeviceSize idx_size = index_data.size_bytes();
        const VkDeviceSize total_size = pos_size + attr_size + idx_size;

        VkBufferCreateInfo staging_buffer_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = total_size,
            .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };

        VmaAllocationCreateInfo staging_alloc_info{
            .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };

        VkBuffer staging_buffer = VK_NULL_HANDLE;
        VmaAllocation staging_allocation = VK_NULL_HANDLE;
        VmaAllocationInfo staging_alloc_result{};

        if (vmaCreateBuffer(allocator, &staging_buffer_info, &staging_alloc_info, &staging_buffer, &staging_allocation, &staging_alloc_result) != VK_SUCCESS) {
            vmaDestroyBuffer(allocator, mesh.position_buffer, mesh.position_allocation);
            vmaDestroyBuffer(allocator, mesh.attribute_buffer, mesh.attribute_allocation);
            vmaDestroyBuffer(allocator, mesh.index_buffer, mesh.index_allocation);
            co_return LogAndMakeError("Failed to create staging buffer");
        }

        auto* mapped_data = static_cast<uint8_t*>(staging_alloc_result.pMappedData);
        std::memcpy(mapped_data, position_data.data(), pos_size);
        std::memcpy(mapped_data + pos_size, attribute_data.data(), attr_size);
        std::memcpy(mapped_data + pos_size + attr_size, index_data.data(), idx_size);

        // 5. Запись и отправка команд копирования
        VkCommandPoolCreateInfo pool_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = device_.GetGraphicsQueueFamily(),
        };

        VkCommandPool cmd_pool = VK_NULL_HANDLE;
        vkCreateCommandPool(vk_device, &pool_info, nullptr, &cmd_pool);

        VkCommandBufferAllocateInfo alloc_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = cmd_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(vk_device, &alloc_info, &cmd);

        VkCommandBufferBeginInfo begin_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        vkBeginCommandBuffer(cmd, &begin_info);

        VkBufferCopy2 copy_pos{
            .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
            .srcOffset = 0,
            .dstOffset = 0,
            .size = pos_size,
        };
        VkCopyBufferInfo2 copy_pos_info{
            .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
            .srcBuffer = staging_buffer,
            .dstBuffer = mesh.position_buffer,
            .regionCount = 1,
            .pRegions = &copy_pos,
        };
        vkCmdCopyBuffer2(cmd, &copy_pos_info);

        VkBufferCopy2 copy_attr{
            .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
            .srcOffset = pos_size,
            .dstOffset = 0,
            .size = attr_size,
        };
        VkCopyBufferInfo2 copy_attr_info{
            .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
            .srcBuffer = staging_buffer,
            .dstBuffer = mesh.attribute_buffer,
            .regionCount = 1,
            .pRegions = &copy_attr,
        };
        vkCmdCopyBuffer2(cmd, &copy_attr_info);

        VkBufferCopy2 copy_index{
            .sType = VK_STRUCTURE_TYPE_BUFFER_COPY_2,
            .srcOffset = pos_size + attr_size,
            .dstOffset = 0,
            .size = idx_size,
        };
        VkCopyBufferInfo2 copy_index_info{
            .sType = VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2,
            .srcBuffer = staging_buffer,
            .dstBuffer = mesh.index_buffer,
            .regionCount = 1,
            .pRegions = &copy_index,
        };
        vkCmdCopyBuffer2(cmd, &copy_index_info);

        vkEndCommandBuffer(cmd);

        VkCommandBufferSubmitInfo cmd_submit_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = cmd,
        };

        VkSubmitInfo2 submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &cmd_submit_info,
        };

        VkFenceCreateInfo fence_info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        VkFence fence = VK_NULL_HANDLE;
        vkCreateFence(vk_device, &fence_info, nullptr, &fence);

        vkQueueSubmit2(device_.GetGraphicsQueue(), 1, &submit_info, fence);
        vkWaitForFences(vk_device, 1, &fence, VK_TRUE, UINT64_MAX);

        // Cleanup
        vkDestroyFence(vk_device, fence, nullptr);
        vkDestroyCommandPool(vk_device, cmd_pool, nullptr);
        vmaDestroyBuffer(allocator, staging_buffer, staging_allocation);

        co_return eastl::move(mesh);
    }

private:
    VulkanDevice& device_;
};

}  // namespace tryengine::graphics