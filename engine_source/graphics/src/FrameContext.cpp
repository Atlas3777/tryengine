#include "engine/graphics/FrameContext.hpp"
#include "engine/core/Assert.hpp"
#include "engine/core/Log.hpp"

namespace tryengine::graphics {

FrameSync::FrameSync(VulkanDevice& device) : device_(device) {
    VkDevice vk_device = device_.GetDevice();

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = device_.GetGraphicsQueueFamily();

    VkSemaphoreCreateInfo semaphore_info{};
    semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    // Fence создаётся в сигнальном состоянии, чтобы первый кадр не блокировался на vkWaitForFences
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        auto& frame = frames_[i];

        TRY_CHECK(vkCreateCommandPool(vk_device, &pool_info, nullptr, &frame.command_pool) == VK_SUCCESS,
                  "Не удалось создать Command Pool для кадра {}", i);

        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = frame.command_pool;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;

        TRY_CHECK(vkAllocateCommandBuffers(vk_device, &alloc_info, &frame.command_buffer) == VK_SUCCESS,
                  "Не удалось выделить Command Buffer для кадра {}", i);

        TRY_CHECK(vkCreateSemaphore(vk_device, &semaphore_info, nullptr, &frame.image_available_semaphore) == VK_SUCCESS,
                  "Не удалось создать image_available_semaphore для кадра {}", i);

        TRY_CHECK(vkCreateFence(vk_device, &fence_info, nullptr, &frame.in_flight_fence) == VK_SUCCESS,
                  "Не удалось создать in_flight_fence для кадра {}", i);
    }
}

FrameSync::~FrameSync() {
    VkDevice vk_device = device_.GetDevice();
    if (!vk_device) return;

    vkDeviceWaitIdle(vk_device);

    for (auto& frame : frames_) {
        if (frame.in_flight_fence) {
            vkDestroyFence(vk_device, frame.in_flight_fence, nullptr);
            frame.in_flight_fence = VK_NULL_HANDLE;
        }
        if (frame.image_available_semaphore) {
            vkDestroySemaphore(vk_device, frame.image_available_semaphore, nullptr);
            frame.image_available_semaphore = VK_NULL_HANDLE;
        }
        if (frame.command_pool) {
            vkDestroyCommandPool(vk_device, frame.command_pool, nullptr);
            frame.command_pool = VK_NULL_HANDLE;
        }
    }
}

bool FrameSync::BeginFrame(VulkanDevice& device, VulkanSwapchain& swapchain, uint32_t& out_image_index) {
    FrameContext& frame = frames_[current_frame_];
    VkDevice vk_device = device.GetDevice();

    // 1. Ожидание fence текущего кадра
    VkResult wait_res = vkWaitForFences(vk_device, 1, &frame.in_flight_fence, VK_TRUE, UINT64_MAX);
    if (wait_res == VK_ERROR_DEVICE_LOST) {
        LogCritical("GPU Device Lost при вызове vkWaitForFences!");
        return false;
    } else if (wait_res != VK_SUCCESS) {
        LogError("Ошибка при вызове vkWaitForFences: {}", static_cast<int>(wait_res));
        return false;
    }

    // 2. Запрос следующей картинки из Swapchain
    VkResult acquire_res = vkAcquireNextImageKHR(
        vk_device, swapchain.GetHandle(), UINT64_MAX,
        frame.image_available_semaphore, VK_NULL_HANDLE, &out_image_index);

    if (acquire_res == VK_ERROR_DEVICE_LOST) {
        LogCritical("GPU Device Lost при вызове vkAcquireNextImageKHR!");
        return false;
    }
    if (acquire_res == VK_ERROR_OUT_OF_DATE_KHR) {
        return false;
    }
    if (acquire_res != VK_SUCCESS && acquire_res != VK_SUBOPTIMAL_KHR) {
        LogError("Ошибка при вызове vkAcquireNextImageKHR: {}", static_cast<int>(acquire_res));
        return false;
    }

    vkResetFences(vk_device, 1, &frame.in_flight_fence);
    vkResetCommandBuffer(frame.command_buffer, 0);

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    VkResult begin_res = vkBeginCommandBuffer(frame.command_buffer, &begin_info);
    if (begin_res == VK_ERROR_DEVICE_LOST) {
        LogCritical("GPU Device Lost при вызове vkBeginCommandBuffer!");
        return false;
    } else if (begin_res != VK_SUCCESS) {
        LogError("Не удалось начать запись Command Buffer: {}", static_cast<int>(begin_res));
        return false;
    }

    return true;
}

bool FrameSync::EndFrameAndPresent(VulkanDevice& device, VulkanSwapchain& swapchain, uint32_t image_index) {
    FrameContext& frame = frames_[current_frame_];

    VkResult end_res = vkEndCommandBuffer(frame.command_buffer);
    if (end_res == VK_ERROR_DEVICE_LOST) {
        LogCritical("GPU Device Lost при вызове vkEndCommandBuffer!");
        return false;
    } else if (end_res != VK_SUCCESS) {
        LogError("Не удалось завершить запись Command Buffer: {}", static_cast<int>(end_res));
        return false;
    }

    VkSemaphore render_finished = swapchain.GetRenderFinishedSemaphore(image_index);

    VkCommandBufferSubmitInfo cmd_info{};
    cmd_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmd_info.commandBuffer = frame.command_buffer;

    VkSemaphoreSubmitInfo wait_info{};
    wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    wait_info.semaphore = frame.image_available_semaphore;
    wait_info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signal_info{};
    signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signal_info.semaphore = render_finished;
    signal_info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkSubmitInfo2 submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit_info.waitSemaphoreInfoCount = 1;
    submit_info.pWaitSemaphoreInfos = &wait_info;
    submit_info.commandBufferInfoCount = 1;
    submit_info.pCommandBufferInfos = &cmd_info;
    submit_info.signalSemaphoreInfoCount = 1;
    submit_info.pSignalSemaphoreInfos = &signal_info;

    // Сбой из-за ошибочного шейдера чаще всего происходит именно во время выполнения на GPU после Submit
    VkResult submit_res = vkQueueSubmit2(device.GetGraphicsQueue(), 1, &submit_info, frame.in_flight_fence);
    if (submit_res == VK_ERROR_DEVICE_LOST) {
        LogCritical("GPU Device Lost при вызове vkQueueSubmit2!");
        return false;
    } else if (submit_res != VK_SUCCESS) {
        LogError("Ошибка при выполнении vkQueueSubmit2: {}", static_cast<int>(submit_res));
        return false;
    }

    VkSwapchainKHR swapchain_handle = swapchain.GetHandle();
    VkPresentInfoKHR present_info{};
    present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.waitSemaphoreCount = 1;
    present_info.pWaitSemaphores = &render_finished;
    present_info.swapchainCount = 1;
    present_info.pSwapchains = &swapchain_handle;
    present_info.pImageIndices = &image_index;

    VkResult present_res = vkQueuePresentKHR(device.GetPresentQueue(), &present_info);

    AdvanceFrame();

    if (present_res == VK_ERROR_DEVICE_LOST) {
        LogCritical("GPU Device Lost при вызове vkQueuePresentKHR!");
        return false;
    }

    if (present_res == VK_ERROR_OUT_OF_DATE_KHR || present_res == VK_SUBOPTIMAL_KHR) {
        return false;
    }

    if (present_res != VK_SUCCESS) {
        LogError("Ошибка при вызове vkQueuePresentKHR: {}", static_cast<int>(present_res));
        return false;
    }

    return true;
}

}  // namespace tryengine::graphics