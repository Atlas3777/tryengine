#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif

#include <volk.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#include <EASTL/string_view.h>

namespace tryengine::graphics {

class VulkanDevice {
public:
    VulkanDevice(uint32_t width, uint32_t height, eastl::string_view title);
    ~VulkanDevice();

    VulkanDevice(const VulkanDevice&) = delete;
    VulkanDevice& operator=(const VulkanDevice&) = delete;
    VulkanDevice(VulkanDevice&&) = delete;
    VulkanDevice& operator=(VulkanDevice&&) = delete;

    [[nodiscard]] SDL_Window* GetWindow() const { return window_; }
    [[nodiscard]] VkInstance GetInstance() const { return instance_; }
    [[nodiscard]] VkPhysicalDevice GetPhysicalDevice() const { return physical_device_; }
    [[nodiscard]] VkDevice GetDevice() const { return device_; }
    [[nodiscard]] VkSurfaceKHR GetSurface() const { return surface_; }
    [[nodiscard]] VmaAllocator GetAllocator() const { return allocator_; }

    [[nodiscard]] VkQueue GetGraphicsQueue() const { return graphics_queue_; }
    [[nodiscard]] uint32_t GetGraphicsQueueFamily() const { return graphics_queue_family_; }
    [[nodiscard]] VkQueue GetPresentQueue() const { return present_queue_; }
    [[nodiscard]] uint32_t GetPresentQueueFamily() const { return present_queue_family_; }

    void WaitIdle() const {
        if (device_) {
            vkDeviceWaitIdle(device_);
        }
    }

private:
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
};

}  // namespace tryengine::graphics