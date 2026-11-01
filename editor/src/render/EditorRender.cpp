#include "editor/render/EditorRender.hpp"

#include "editor/render/DebugPass.hpp"
#include "editor/render/ImGuiPass.hpp"
#include "editor/render/PickingPass.hpp"
#include "engine/core/Assert.hpp"
#include "engine/core/Engine.hpp"
#include "engine/graphics/OpaquePass.hpp"
#include "engine/graphics/PresentPass.hpp"
#include "engine/graphics/VulkanDevice.hpp"
#include "engine/resources/ResourceManager.hpp"

namespace tryeditor {

// TODO: Задержка инвалидации (Debouncing / Resize End)
// Во время взаимодействия с GUI (когда пользователь зажал зажатый края окна и тянет его):
// Во время драга не пересоздавать текстуры сцены каждый кадр, а растягивать имеющуюся текстуру через UV в ImGui.
// Инвалидировать граф и создавать физические ресурсы нового размера только тогда, когда пользователь отпустил мышь

EditorRender::EditorRender(tryengine::core::Engine& engine, tryengine::graphics::VulkanDevice& device,
                           tryengine::graphics::RenderGraph& render_graph,
                           tryengine::graphics::VulkanSwapchain& swapchain, tryengine::graphics::FrameSync& frame_sync)
    : rg_(render_graph), swapchain_(swapchain), frame_sync_(frame_sync) {
    auto deb_shader =
        engine.Get<tryengine::resources::ResourceManager>().Get<tryengine::graphics::Shader>(10245112668937909950ULL);
    TRY_ASSERT(deb_shader.has_value(), "Нет шейдера debug");

    auto pick_shader =
        engine.Get<tryengine::resources::ResourceManager>().Get<tryengine::graphics::Shader>(4250482409988402633ULL);
    TRY_ASSERT(pick_shader.has_value(), "Нет шейдера picking");

    rg_.AddPass<ImGuiPass>(device, swapchain);
    rg_.AddPass<tryengine::graphics::OpaquePass>();
    rg_.AddPass<DebugPass>(*deb_shader);
    rg_.AddPass<PickingPass>(device, *pick_shader);
    rg_.AddPass<tryengine::graphics::PresentPass>();
}

EditorRender::~EditorRender() {
    rg_.GetDevice().WaitIdle();
}

void EditorRender::RecreateSwapchain(int w, int h, const tryengine::graphics::VulkanDevice& device) const {
    device.WaitIdle();

    if (w == 0 || h == 0)
        return;

    swapchain_.Recreate(static_cast<uint32_t>(w), static_cast<uint32_t>(h), swapchain_.IsVsync());
}

void EditorRender::Render(tryengine::core::Engine& engine, tryengine::graphics::VulkanDevice& device,
                          PlayModeState& state) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(device.GetWindow(), &w, &h);

    if (w == 0 || h == 0) {
        return;
    }

    const VkExtent2D current_extent = swapchain_.GetExtent();
    if (static_cast<uint32_t>(w) != current_extent.width || static_cast<uint32_t>(h) != current_extent.height) {
        RecreateSwapchain(w, h, device);
        return;
    }

    uint32_t image_index = 0;
    if (!frame_sync_.BeginFrame(device, swapchain_, image_index)) {
        RecreateSwapchain(w, h, device);
        return;
    }

    const uint32_t frame_index = frame_sync_.GetCurrentFrameIndex();
    VkCommandBuffer cmd = frame_sync_.GetCurrentFrame().command_buffer;

    rg_.BeginFrame(frame_index);

    ImportSwapchain(image_index);

    auto& script = engine.Get<tryengine::core::ScriptSystem>();
    auto size_res = script.SimpleReturnUnsafe<das::float2>("GetEditorViewportSize");
    VkExtent2D scene_viewport{1, 1};
    if (size_res.has_value()) {
        scene_viewport = {std::max(1u, (uint32_t) size_res->x), std::max(1u, (uint32_t) size_res->y)};
    }

    if (scene_viewport.width != last_viewport_size_.width || scene_viewport.height != last_viewport_size_.height) {
        rg_.Invalidate();
        last_viewport_size_ = scene_viewport;
    }

    auto camera_res =
        script.SimpleReturnUnsafe<tryengine::graphics::CameraData*>("get_camera", size_res.value_or(das::float2(1, 1)));
    tryengine::graphics::CameraData cam_data =
        camera_res.has_value() ? *(*camera_res) : tryengine::graphics::CameraData{};

    rg_.GetCPUBlackboard().Put(tryengine::graphics::RGTag_v<"ViewportSize">, scene_viewport);
    rg_.GetCPUBlackboard().Put(tryengine::graphics::RGTag_v<"Camera">, cam_data);

    rg_.Collect(engine);
    rg_.Compile();
    rg_.Execute(cmd);

    if (!frame_sync_.EndFrameAndPresent(device, swapchain_, image_index)) {
        RecreateSwapchain(w, h, device);
    }
}

void EditorRender::ImportSwapchain(uint32_t image_index) const {
    const VkExtent2D swap_extent = swapchain_.GetExtent();
    const tryengine::graphics::RGTextureDesc swap_desc{.width = swap_extent.width,
                                                       .height = swap_extent.height,
                                                       .layers = 1,
                                                       .mips = 1,
                                                       .format = swapchain_.GetImageFormat(),
                                                       .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                                       .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                       .debug_name = "SwapchainTexture"};

    rg_.ImportExternalTexture(tryengine::graphics::RGTag_v<"Swapchain">, swapchain_.GetImages()[image_index],
                              swapchain_.GetImageViews()[image_index], swap_desc, VK_IMAGE_LAYOUT_UNDEFINED);
}

}  // namespace tryeditor