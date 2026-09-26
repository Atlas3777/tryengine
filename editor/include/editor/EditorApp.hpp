#pragma once

#include <memory>

#include "editor/Editor.hpp"
#include "engine/core/Engine.hpp"

namespace tryengine::graphics {
class FrameSync;
class VulkanSwapchain;
}
namespace tryeditor {
class EditorApp {
public:
    EditorApp();
    ~EditorApp();
    EditorApp(const EditorApp&) = delete;
    EditorApp& operator=(const EditorApp&) = delete;

    void Init();
    void Run() const;
    void Shutdown() const;

private:
    std::unique_ptr<tryengine::graphics::VulkanDevice> vulkan_device_;
    std::unique_ptr<tryengine::graphics::VulkanSwapchain> swapchain_;
    std::unique_ptr<tryengine::graphics::FrameSync> frame_sync_;
    std::unique_ptr<tryengine::core::Engine> engine_;
    std::unique_ptr<Editor> editor_;
};
}  // namespace tryeditor
