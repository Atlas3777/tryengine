#pragma once

#include <memory>

#include "engine/core/Engine.hpp"
#include "engine/graphics/FrameContext.hpp"
#include "engine/graphics/GraphicsContext.hpp"
#include "engine/graphics/VulkanDevice.hpp"
#include "engine/resources/AsyncFileManager.hpp"

namespace trygame {

class GameApp {
public:
    GameApp() = default;
    void Init();
    void Run();
    void Shutdown();
private:
    bool running_;

    std::unique_ptr<tryengine::graphics::VulkanDevice> vulkan_device_;
    std::unique_ptr<tryengine::graphics::VulkanSwapchain> swapchain_;
    std::unique_ptr<tryengine::graphics::FrameSync> frame_sync_;
    std::unique_ptr<tryengine::core::Engine> engine_;
};

}  // namespace trygame