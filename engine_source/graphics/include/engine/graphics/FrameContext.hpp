#pragma once

#include <volk.h>
#include <EASTL/array.h>
#include "engine/graphics/VulkanDevice.hpp"
#include "engine/graphics/VulkanSwapchain.hpp"

namespace tryengine::graphics {

constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

struct FrameContext {
    VkCommandPool command_pool = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;

    VkSemaphore image_available_semaphore = VK_NULL_HANDLE;
    VkFence in_flight_fence = VK_NULL_HANDLE;
};

class FrameSync {
public:
    explicit FrameSync(VulkanDevice& device);
    ~FrameSync();

    FrameSync(const FrameSync&) = delete;
    FrameSync& operator=(const FrameSync&) = delete;
    FrameSync(FrameSync&&) = delete;
    FrameSync& operator=(FrameSync&&) = delete;

    [[nodiscard]] FrameContext& GetCurrentFrame() { return frames_[current_frame_]; }
    [[nodiscard]] const FrameContext& GetCurrentFrame() const { return frames_[current_frame_]; }
    [[nodiscard]] uint32_t GetCurrentFrameIndex() const { return current_frame_; }

    /// Ожидает готовности кадра на GPU, запрашивает следующий индекс Swapchain и начинает запись Command Buffer.
    /// Возвращает false, если Swapchain устарел (требуется пересоздание).
    bool BeginFrame(VulkanDevice& device, VulkanSwapchain& swapchain, uint32_t& out_image_index);

    /// Завершает запись Command Buffer, отправляет командный буфер через vkQueueSubmit2 и презентует кадр.
    /// Возвращает false, если Swapchain устарел при презентации.
    bool EndFrameAndPresent(VulkanDevice& device, VulkanSwapchain& swapchain, uint32_t image_index);

    void AdvanceFrame() { current_frame_ = (current_frame_ + 1) % MAX_FRAMES_IN_FLIGHT; }

private:
    VulkanDevice& device_;
    eastl::array<FrameContext, MAX_FRAMES_IN_FLIGHT> frames_{};
    uint32_t current_frame_ = 0;
};

}  // namespace tryengine::graphics