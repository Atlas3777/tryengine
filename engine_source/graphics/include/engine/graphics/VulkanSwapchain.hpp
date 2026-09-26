#pragma once

#include <EASTL/vector.h>
#include <volk.h>
#include <VkBootstrap.h>
#include "engine/graphics/VulkanDevice.hpp"

namespace tryengine::graphics {

class VulkanSwapchain {
public:
    explicit VulkanSwapchain(VulkanDevice& device, uint32_t width, uint32_t height, bool vsync = true);
    ~VulkanSwapchain();

    VulkanSwapchain(const VulkanSwapchain&) = delete;
    VulkanSwapchain& operator=(const VulkanSwapchain&) = delete;

    void Recreate(uint32_t width, uint32_t height, bool vsync);

    [[nodiscard]] VkSwapchainKHR GetHandle() const { return swapchain_; }
    [[nodiscard]] VkFormat GetImageFormat() const { return image_format_; }
    [[nodiscard]] VkExtent2D GetExtent() const { return extent_; }
    [[nodiscard]] const eastl::vector<VkImage>& GetImages() const { return images_; }
    [[nodiscard]] const eastl::vector<VkImageView>& GetImageViews() const { return image_views_; }
    [[nodiscard]] VkSemaphore GetRenderFinishedSemaphore(uint32_t image_index) const {
        return render_finished_semaphores_[image_index];
    }
    [[nodiscard]] bool IsVsync() const { return vsync_; }

private:
    void Cleanup();
    void Build(uint32_t width, uint32_t height, bool vsync);
    bool vsync_ = true;

    VulkanDevice& device_;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat image_format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{0, 0};

    eastl::vector<VkImage> images_;
    eastl::vector<VkImageView> image_views_;
    eastl::vector<VkSemaphore> render_finished_semaphores_;
};

}  // namespace tryengine::graphics