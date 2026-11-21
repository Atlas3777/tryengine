#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <EASTL/string_view.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#include <volk.h>

namespace tryengine::graphics {

// 1x1 белая текстура для сэмплеров, которые материал не привязал. Дескриптор в наборе
// материала обязан быть валидным, даже если шейдер читает текстуру только в одной ветке:
// без заглушки vkCmdDrawIndexedIndirect падает с VUID-vkCmdDrawIndexedIndirect-None-08114.
struct DefaultTexture {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
};

class VulkanDevice {
public:
    VulkanDevice(uint32_t width, uint32_t height, eastl::string_view title);
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;
    VulkanDevice(VulkanDevice&&) = delete;
    VulkanDevice& operator=(VulkanDevice&&) = delete;

    [[nodiscard]] SDL_Window* GetWindow() const {
        return window_;
    }

    [[nodiscard]] VkInstance GetInstance() const {
        return instance_;
    }

    [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const {
        return physical_device_;
    }

    [[nodiscard]] VkDevice GetDevice() const {
        return device_;
    }

    [[nodiscard]] VkSurfaceKHR GetSurface() const {
        return surface_;
    }

    [[nodiscard]] VmaAllocator GetAllocator() const {
        return allocator_;
    }

    [[nodiscard]] VkQueue GetGraphicsQueue() const {
        return graphics_queue_;
    }

    [[nodiscard]] uint32_t GetGraphicsQueueFamily() const {
        return graphics_queue_family_;
    }

    [[nodiscard]] VkQueue GetPresentQueue() const {
        return present_queue_;
    }

    [[nodiscard]] uint32_t GetPresentQueueFamily() const {
        return present_queue_family_;
    }

    [[nodiscard]] const DefaultTexture& GetDefaultWhiteTexture() const {
        return default_white_texture_;
    }

    void WaitIdle() const {
        if (device_)
            vkDeviceWaitIdle(device_);
    }

private:
    void CreateDefaultWhiteTexture();
    void DestroyDefaultWhiteTexture();

    SDL_Window* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    VkQueue graphics_queue_ = VK_NULL_HANDLE;
    uint32_t graphics_queue_family_ = 0;
    VkQueue present_queue_ = VK_NULL_HANDLE;
    uint32_t present_queue_family_ = 0;

    DefaultTexture default_white_texture_;
};

}  // namespace tryengine::graphics