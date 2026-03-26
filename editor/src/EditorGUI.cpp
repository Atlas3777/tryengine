#include "editor/EditorGUI.hpp"

#include <ImGuizmo.h>

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <algorithm>
#include <glm/gtx/matrix_decompose.hpp>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include "editor/Components.hpp"
#include "editor/GameViewportPanel.hpp"
#include "editor/HierarchyPanel.hpp"
#include "editor/InspectorPanel.hpp"
#include "editor/SceneViewportPanel.hpp"
#include "engine/core/Components.hpp"
#include "engine/core/Engine.hpp"
#include "engine/graphics/RenderTarget.hpp"

namespace editor {
using namespace engine::core;
using namespace engine;

EditorGUI::EditorGUI(const engine::graphics::GraphicsContext& context) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    // Настройка бэкендов
    ImGui_ImplSDL3_InitForSDLGPU(context.GetWindow());

    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device = context.GetDevice();
    init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(context.GetDevice(), context.GetWindow());
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    ImGui_ImplSDLGPU3_Init(&init_info);

    panels_.emplace_back(std::make_unique<SceneViewportPanel>(context.GetDevice()));
    panels_.emplace_back(std::make_unique<GameViewportPanel>(context.GetDevice()));
    panels_.emplace_back(std::make_unique<InspectorPanel>());
    panels_.emplace_back(std::make_unique<HierarchyPanel>());
}

EditorGUI::~EditorGUI() {
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void EditorGUI::RecordPanelsGpuCommands(const Engine& engine) {
    // 1. Начало кадра ImGui
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    DrawDockSpace();


    for (const auto& panel : panels_) {
        panel->OnImGuiRender(engine.GetSceneManager().GetActiveScene()->GetRegistry());
    }

    // ImGui::ShowDemoWindow();

    // 4. Финализация
    ImGui::Render();
}

void EditorGUI::RenderToPanel(SDL_GPUCommandBuffer* cmd, graphics::RenderSystem& render_system, Engine& engine) const {
    for (const auto& panel : panels_) {
        panel->OnRender(cmd, render_system, engine.GetSceneManager().GetActiveScene()->GetRegistry());
    }
    // ImGui::ShowDemoWindow();
}

void EditorGUI::RenderPanelsToSwapchain(SDL_GPUTexture* swapchainTexture, SDL_GPUCommandBuffer* cmd) {
    ImDrawData* draw_data = ImGui::GetDrawData();
    ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, cmd);

    SDL_GPUColorTargetInfo colorInfo{};
    colorInfo.texture = swapchainTexture;
    colorInfo.clear_color = {0, 0, 0, 1};
    colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    colorInfo.store_op = SDL_GPU_STOREOP_STORE;

    auto guiPass = SDL_BeginGPURenderPass(cmd, &colorInfo, 1, nullptr);

    ImGui_ImplSDLGPU3_RenderDrawData(draw_data, cmd, guiPass);
    SDL_EndGPURenderPass(guiPass);

    SDL_SubmitGPUCommandBuffer(cmd);
}

void EditorGUI::DrawDockSpace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
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

// void EditorGUI::DrawEngineControl(engine::core::Engine& engine) {
//     ImGui::Begin("Engine Control");
//
//     // // Массив строк для отображения в комбо-боксе
//     // const char* modes[] = {"Immediate (Uncapped)", "Mailbox (Fast)", "VSync (Locked)"};
//     // int currentItem = static_cast<int>(engine.settings.presentMode);
//     //
//     // if (ImGui::Combo("Sync Mode", &currentItem, modes, IM_ARRAYSIZE(modes))) {
//     //     engine.settings.presentMode = static_cast<engine::PresentMode>(currentItem);
//     //     engine.PushCommand(engine::CmdSetPresentMode(engine.settings.presentMode));
//     // }
//
//     ImGui::Separator();
//     ImGui::Text("FPS: %d", engine.GetClock().GetFPS());
//
//     // Подсказка для понимания разницы
//     // if (currentItem == 0) ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Warning: Screen tearing possible");
//
//     ImGui::End();
// }



}  // namespace editor
