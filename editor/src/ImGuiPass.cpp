#include "editor/ImGuiPass.hpp"

#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include "engine/core/Assert.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/ScriptSystem.hpp"
#include "engine/graphics/VulkanDevice.hpp"
#include "engine/graphics/VulkanSwapchain.hpp"

namespace tryeditor {

ImGuiPass::ImGuiPass(tryengine::graphics::VulkanDevice& device, tryengine::graphics::VulkanSwapchain& swapchain)
    : device_(device), swapchain_(swapchain) {

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "editor/imgui.ini";
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForVulkan(device.GetWindow());

    // 1. Создание дескрипторного пула для ImGui
    VkDescriptorPoolSize pool_sizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64},
    };
    VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 64;
    pool_info.poolSizeCount = 1;
    pool_info.pPoolSizes = pool_sizes;

    TRY_CHECK(vkCreateDescriptorPool(device.GetDevice(), &pool_info, nullptr, &descriptor_pool_) == VK_SUCCESS,
              "Не удалось создать VkDescriptorPool для ImGuiPass");

    // 2. Инициализация Vulkan-бэкенда ImGui
    VkFormat swap_format = swapchain.GetImageFormat();

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.ApiVersion = VK_API_VERSION_1_4;
    init_info.Instance = device.GetInstance();
    init_info.PhysicalDevice = device.GetPhysicalDevice();
    init_info.Device = device.GetDevice();
    init_info.QueueFamily = device.GetGraphicsQueueFamily();
    init_info.Queue = device.GetGraphicsQueue();
    init_info.DescriptorPool = descriptor_pool_;
    init_info.MinImageCount = 2;
    init_info.ImageCount = static_cast<uint32_t>(swapchain.GetImages().size());
    init_info.UseDynamicRendering = true;

    VkPipelineRenderingCreateInfo rendering_info{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &swap_format;

    init_info.PipelineInfoMain.PipelineRenderingCreateInfo = rendering_info;
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_LoadFunctions(
        VK_API_VERSION_1_4,
        [](const char* function_name, void* user_data) {
            return vkGetInstanceProcAddr(static_cast<VkInstance>(user_data), function_name);
        },
        device.GetInstance());

    ImGui_ImplVulkan_Init(&init_info);

    // 3. Создание сэмплера для ImGui::Image
    VkSamplerCreateInfo sampler_info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler_info.magFilter = VK_FILTER_LINEAR;
    sampler_info.minFilter = VK_FILTER_LINEAR;
    sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    TRY_CHECK(vkCreateSampler(device.GetDevice(), &sampler_info, nullptr, &sampler_) == VK_SUCCESS,
              "Не удалось создать VkSampler для ImGuiPass");
}

ImGuiPass::~ImGuiPass() {
    vkDeviceWaitIdle(device_.GetDevice());

    ClearTextureCache();

    if (sampler_) {
        vkDestroySampler(device_.GetDevice(), sampler_, nullptr);
    }

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (descriptor_pool_) {
        vkDestroyDescriptorPool(device_.GetDevice(), descriptor_pool_, nullptr);
    }
}

void ImGuiPass::Setup(tryengine::graphics::RenderGraphBuilder& builder) {
    // В редиректной схеме запрашиваем теги сцены и swapchain
    data_.scene_color_read = builder.Read(tryengine::graphics::RGTag_v<"ForwardColor">, tryengine::graphics::RGUsageHint::ShaderRead);
    data_.swapchain_target = builder.Write(tryengine::graphics::RGTag_v<"Swapchain">, tryengine::graphics::RGUsageHint::ColorAttachment);

    // Пасс выводит кадр на экран, предотвращаем culling
    builder.MarkSideEffect();
}

void ImGuiPass::Collect(tryengine::graphics::CollectContext& ctx) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ctx.engine.Get<tryengine::core::ScriptSystem>().InvokeFunctionSafe("DrawEditor");

    ImGui::Render();
}

void ImGuiPass::Execute(tryengine::graphics::RGExecuteContext& ctx) {
    ImDrawData* draw_data = ImGui::GetDrawData();
    if (!draw_data || draw_data->CmdListsCount == 0) {
        return;
    }

    VkExtent2D swap_extent = swapchain_.GetExtent();

    tryengine::graphics::RGColorAttachment color_att{};
    color_att.handle = data_.swapchain_target;
    color_att.load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_att.store_op = VK_ATTACHMENT_STORE_OP_STORE;
    color_att.clear_value = {{{0.0f, 0.0f, 0.0f, 1.0f}}};

    VkRect2D render_area{{0, 0}, swap_extent};

    ctx.BeginRendering(eastl::span(&color_att, 1), nullptr, render_area, 1);
    ImGui_ImplVulkan_RenderDrawData(draw_data, ctx.cmd_buffer);
    ctx.EndRendering();
}

VkDescriptorSet ImGuiPass::GetOrCreateTexture(VkImageView view) {
    if (!view) {
        return VK_NULL_HANDLE;
    }

    auto it = texture_cache_.find(view);
    if (it != texture_cache_.end()) {
        return it->second;
    }

    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(sampler_, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    texture_cache_[view] = ds;
    return ds;
}

void ImGuiPass::ClearTextureCache() {
    device_.WaitIdle();
    for (auto [view, ds] : texture_cache_) {
        ImGui_ImplVulkan_RemoveTexture(ds);
    }
    LogInfo("ClearTextureCache");
    texture_cache_.clear();
}

} // namespace tryeditor