#include "editor/EditorRender.hpp"

#include <cstring>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include "editor/CollectDebug.hpp"
#include "editor/PlayModeState.hpp"
#include "engine/core/Assert.hpp"
#include "engine/graphics/ForwardPipeline.hpp"
#include "engine/graphics/OpaqueGeometryPass.hpp"
#include "engine/graphics/VulkanDevice.hpp"
#include "engine/core/HlslppFormatter.hpp"

namespace tryeditor {

using tryengine::graphics::RenderGraphBuilder;
using tryengine::graphics::RGBufferDesc;
using tryengine::graphics::RGColorAttachment;
using tryengine::graphics::RGDepthStencilAttachment;
using tryengine::graphics::RGExecuteContext;
using tryengine::graphics::RGResourceHandle;
using tryengine::graphics::RGTextureDesc;
using tryengine::graphics::RGUsageHint;

EditorRender::EditorRender(tryengine::graphics::VulkanDevice& device, tryengine::graphics::RenderGraph& render_graph,
                           tryengine::graphics::VulkanSwapchain& swapchain, tryengine::graphics::FrameSync& frame_sync)
    : rg_(render_graph), swapchain_(swapchain), frame_sync_(frame_sync) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "editor/imgui.ini";
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForVulkan(device.GetWindow());

    // Отдельный дескриптор-пул под ImGui (шрифты + возможные ImGui::Image).
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64},
    };
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 64;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = pool_sizes;
    TRY_CHECK(vkCreateDescriptorPool(device.GetDevice(), &pool_info, nullptr, &imgui_descriptor_pool_) == VK_SUCCESS,
              "Не удалось создать VkDescriptorPool для ImGui");

    VkFormat swap_format = swapchain_.GetImageFormat();

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = VK_API_VERSION_1_4;
    init_info.Instance = device.GetInstance();
    init_info.PhysicalDevice = device.GetPhysicalDevice();
    init_info.Device = device.GetDevice();
    init_info.QueueFamily = device.GetGraphicsQueueFamily();
    init_info.Queue = device.GetGraphicsQueue();
    init_info.DescriptorPool = imgui_descriptor_pool_;
    init_info.MinImageCount = 2;
    init_info.ImageCount = static_cast<uint32_t>(swapchain_.GetImages().size());
    init_info.UseDynamicRendering = true;

    VkPipelineRenderingCreateInfo rendering_info{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &swap_format;

    init_info.PipelineInfoMain.PipelineRenderingCreateInfo = rendering_info;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    // ВАЖНО: Загружаем табличные указатели Vulkan-функций для ImGui
    ImGui_ImplVulkan_LoadFunctions(
        VK_API_VERSION_1_4,
        [](const char* function_name, void* user_data) {
            return vkGetInstanceProcAddr(static_cast<VkInstance>(user_data), function_name);
        },
        device.GetInstance()
    );

    // Теперь вызов инициализации не упадет с SIGSEGV
    ImGui_ImplVulkan_Init(&init_info);

    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    vkCreateSampler(device.GetDevice(), &sampler_info, nullptr, &imgui_sampler_);
}

EditorRender::~EditorRender() {
    tryengine::graphics::VulkanDevice& device = rg_.GetDevice();
    vkDeviceWaitIdle(device.GetDevice());

    for (auto& frame_staging : pending_debug_staging_) {
        for (auto& s : frame_staging) {
            vmaDestroyBuffer(device.GetAllocator(), s.buffer, s.allocation);
        }
        frame_staging.clear();
    }
    if (imgui_sampler_) {
        vkDestroySampler(device.GetDevice(), imgui_sampler_, nullptr);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (imgui_descriptor_pool_) {
        vkDestroyDescriptorPool(device.GetDevice(), imgui_descriptor_pool_, nullptr);
        imgui_descriptor_pool_ = VK_NULL_HANDLE;
    }
}

VkDescriptorSet EditorRender::GetOrCreateImguiTexture(VkImageView view) {
    if (!view) return VK_NULL_HANDLE;

    // 1. Если дескриптор для этого VkImageView уже есть — возвращаем его без вызова AddTexture
    auto it = imgui_texture_cache_.find(view);
    if (it != imgui_texture_cache_.end()) {
        return it->second;
    }

    // 2. Создаем дескриптор ТОЛЬКО если это новый VkImageView
    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(
        imgui_sampler_,
        view,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    );

    imgui_texture_cache_[view] = ds;
    return ds;
}

void EditorRender::ClearTextureCache() {
    for (auto [view, ds] : imgui_texture_cache_) {
        ImGui_ImplVulkan_RemoveTexture(ds);
    }
    imgui_texture_cache_.clear();
}

void EditorRender::RecordImguiFrame(tryengine::core::Engine& engine, PlayModeState& state) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // DrawMainMenu();
    // DrawPlayToolbar(state);
    DrawDockSpace();

    engine.Get<tryengine::core::ScriptSystem>().InvokeFunctionSafe("draw_editor");

    // RenderProfilerPanel();

    ImGui::Render();
}

void EditorRender::DestroyPendingDebugStaging(uint32_t frame_index) {
    tryengine::graphics::VulkanDevice& device = rg_.GetDevice();
    auto& frame_staging = pending_debug_staging_[frame_index];
    for (auto& s : frame_staging) {
        vmaDestroyBuffer(device.GetAllocator(), s.buffer, s.allocation);
    }
    frame_staging.clear();
}

void EditorRender::RecreateSwapchain(int w, int h, tryengine::graphics::VulkanDevice& device) {
    // 1. Ждем полной остановки GPU перед очисткой ресурсов
    vkDeviceWaitIdle(device.GetDevice());

    // 2. Безопасно очищаем кэш дескрипторов ImGui — GPU гарантированно ничего не рисует
    ClearTextureCache();

    if (w == 0 || h == 0) return;

    swapchain_.Recreate(static_cast<uint32_t>(w), static_cast<uint32_t>(h), swapchain_.IsVsync());
}

void EditorRender::Render(tryengine::core::Engine& engine, tryengine::graphics::VulkanDevice& device,
                          PlayModeState& state) {
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(device.GetWindow(), &w, &h);

    // Если окно свернуто — пропуск кадра
    if (w == 0 || h == 0) {
        return;
    }

    VkExtent2D current_extent = swapchain_.GetExtent();
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
    DestroyPendingDebugStaging(frame_index);

    VkCommandBuffer cmd = frame_sync_.GetCurrentFrame().command_buffer;

    auto& script_system = engine.Get<tryengine::core::ScriptSystem>();


    tryengine::graphics::FrameRenderData frame_render_data;
    frame_render_data.point_lights = eastl::move(tryengine::graphics::CollectLight(engine));
    frame_render_data.opaque_queue = eastl::move(tryengine::graphics::OpaqueGeometryPass::CollectDrawable(engine));

    rg_.BeginFrame(frame_index);

    VkExtent2D swap_extent = swapchain_.GetExtent();
    RGTextureDesc swap_desc{swap_extent.width, swap_extent.height, 1, 1,
                            swapchain_.GetImageFormat(),
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                            VK_IMAGE_ASPECT_COLOR_BIT,
                            "SwapchainTexture"};

    RGResourceHandle swapchain_handle =
        rg_.ImportExternalTexture("Swapchain", swapchain_.GetImages()[image_index],
                                  swapchain_.GetImageViews()[image_index], swap_desc, VK_IMAGE_LAYOUT_UNDEFINED);

    auto size_res = script_system.SimpleReturnUnsafe<das::float2>("GetEditorViewportSize");
    if (!size_res.has_value())
        LogError("aaa");

    auto size = *size_res;

    auto camera_data = script_system.SimpleReturnUnsafe<tryengine::graphics::CameraData*>("get_camera", size);
    if (!camera_data.has_value())
        LogCritical("Camera data not found");

    const auto camera = *camera_data;

    tryengine::graphics::GlobalLight t;
    t.ambient_color = hlslpp::float4(0.1f, 0.1f, 0.1f, 0);
    t.view_pos = hlslpp::float4(camera->position, 0);

    rg_.PutCPUData(RGTag_v<"GlobalLightUBO">, t);
    rg_.PutCPUData(RGTag_v<"Camera">, *camera);
    rg_.PutCPUData(RGTag_v<"LightCount">, static_cast<uint32_t>(frame_render_data.point_lights.size()));

    EditorFrame editor_frame;
    editor_frame.debug_lines = eastl::move(CollectDebug(engine));

    BuildEditorRenderGraph(frame_render_data, editor_frame, swapchain_handle, static_cast<uint32_t>(size.x),
                           static_cast<uint32_t>(size.y));

    rg_.Compile();
    RecordImguiFrame(engine, state);
    rg_.Execute(cmd);

    if (!frame_sync_.EndFrameAndPresent(device, swapchain_, image_index)) {
        RecreateSwapchain(w, h, device);
    }
}

void EditorRender::BuildEditorRenderGraph(const tryengine::graphics::FrameRenderData& frame_render_data,
                                          const EditorFrame& editor_frame, RGResourceHandle swapchain_handle,
                                          uint32_t width, uint32_t height) const {
    auto forward_out = tryengine::graphics::BuildForwardPipeline(rg_, frame_render_data, width, height);

    const VkExtent2D swap_extent = swapchain_.GetExtent();

    auto& imgui = rg_.AddPass<ImGuiPassData>(
        "ImGuiPass",
        [&](RenderGraphBuilder& builder, ImGuiPassData& data) {
            data.scene_tex_read = builder.Read(forward_out.color_target, RGUsageHint::ShaderRead);
            data.swapchain_target = builder.Write(swapchain_handle, RGUsageHint::ColorAttachment);
            builder.MarkSideEffect();
        },
        [swap_extent](RGExecuteContext& ctx, const ImGuiPassData& data) {
            ImDrawData* draw_data = ImGui::GetDrawData();
            if (!draw_data || draw_data->CmdListsCount == 0)
                return;

            RGColorAttachment color_att{};
            color_att.handle = data.swapchain_target;
            color_att.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
            color_att.store_op = VK_ATTACHMENT_STORE_OP_STORE;
            color_att.clear_value = {{{0.0f, 0.0f, 0.0f, 1.0f}}};

            VkRect2D render_area{{0, 0}, swap_extent};

            ctx.BeginRendering(eastl::span(&color_att, 1), nullptr, render_area, 1);
            ImGui_ImplVulkan_RenderDrawData(draw_data, ctx.cmd_buffer);
            ctx.EndRendering();
        });
    tryengine::graphics::AddPresentPass(rg_, imgui.swapchain_target);
}

void EditorRender::DrawDockSpace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    float toolbar_height = 0.0f;  // 30.0f;

    ImVec2 dock_pos = ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + toolbar_height);
    ImVec2 dock_size = ImVec2(viewport->WorkSize.x, viewport->WorkSize.y - toolbar_height);

    ImGui::SetNextWindowPos(dock_pos);
    ImGui::SetNextWindowSize(dock_size);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags host_window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                         ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("MainDockSpace", nullptr, host_window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MainDockSpaceDock");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();
}

}  // namespace tryeditor