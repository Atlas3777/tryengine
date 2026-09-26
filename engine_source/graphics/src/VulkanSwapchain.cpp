#include "engine/graphics/VulkanSwapchain.hpp"
#include "engine/core/Assert.hpp"

namespace tryengine::graphics {

VulkanSwapchain::VulkanSwapchain(VulkanDevice& device, const uint32_t width, const uint32_t height, const bool vsync)
    : device_(device) {
    Build(width, height, vsync);
}

VulkanSwapchain::~VulkanSwapchain() {
    vkDeviceWaitIdle(device_.GetDevice());
    Cleanup();
}

void VulkanSwapchain::Recreate(const uint32_t width, const uint32_t height, const bool vsync) {
    vkDeviceWaitIdle(device_.GetDevice());
    Cleanup();
    Build(width, height, vsync);
}

void VulkanSwapchain::Build(const uint32_t width, const uint32_t height, const bool vsync) {
    vsync_ = vsync;
    vkb::SwapchainBuilder builder{device_.GetPhysicalDevice(), device_.GetDevice(), device_.GetSurface()};

    auto ret = builder.set_desired_extent(width, height)
                   .set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                   .set_desired_present_mode(vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR)
                   .add_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
                   .build();

    TRY_CHECK(ret.has_value(), "Не удалось создать Swapchain: {}", ret.error().message());

    vkb::Swapchain vkb_swapchain = ret.value();
    swapchain_ = vkb_swapchain.swapchain;
    image_format_ = vkb_swapchain.image_format;
    extent_ = vkb_swapchain.extent;

    auto images_ret = vkb_swapchain.get_images();
    TRY_CHECK(images_ret.has_value(), "Не удалось получить изображения Swapchain");

    const auto& std_images = images_ret.value();
    images_.assign(std_images.data(), std_images.data() + std_images.size());

    auto views_ret = vkb_swapchain.get_image_views();
    TRY_CHECK(views_ret.has_value(), "Не удалось получить ImageView Swapchain");

    const auto& std_views = views_ret.value();
    image_views_.assign(std_views.data(), std_views.data() + std_views.size());

    // NEW: по семафору на каждую картинку swapchain (не на кадр в полёте)
    VkSemaphoreCreateInfo sem_info{};
    sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    render_finished_semaphores_.resize(images_.size(), VK_NULL_HANDLE);
    for (auto& sem : render_finished_semaphores_) {
        TRY_CHECK(vkCreateSemaphore(device_.GetDevice(), &sem_info, nullptr, &sem) == VK_SUCCESS,
                  "Не удалось создать render_finished семафор для swapchain");
    }
}

void VulkanSwapchain::Cleanup() {
    VkDevice dev = device_.GetDevice();

    for (auto sem : render_finished_semaphores_) {
        if (sem) vkDestroySemaphore(dev, sem, nullptr);
    }
    render_finished_semaphores_.clear();

    for (auto view : image_views_) {
        vkDestroyImageView(dev, view, nullptr);
    }
    image_views_.clear();
    images_.clear();

    if (swapchain_) {
        vkDestroySwapchainKHR(dev, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

}  // namespace tryengine::graphics