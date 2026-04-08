#include "editor/gui/EditorGUI.hpp"

#include <ImGuizmo.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include "editor/gui/FileBrowserPanel.hpp"
#include "editor/gui/GameViewportPanel.hpp"
#include "editor/gui/HierarchyPanel.hpp"
#include "editor/gui/InspectorPanel.hpp"
#include "editor/gui/SceneViewportPanel.hpp"
#include "engine/core/Engine.hpp"

namespace tryeditor {

EditorGUI::EditorGUI(const tryengine::graphics::GraphicsContext& context, ImportSystem& import_system, Spawner& spawner) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    static std::string ini_path = (std::filesystem::current_path() / "tryeditor" / "imgui.ini").string();
    io.IniFilename = ini_path.c_str();

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

    panels_.emplace_back(std::make_unique<SceneViewportPanel>(context.GetDevice(), spawner));
    panels_.emplace_back(std::make_unique<GameViewportPanel>(context.GetDevice()));
    panels_.emplace_back(std::make_unique<InspectorPanel>());
    panels_.emplace_back(std::make_unique<HierarchyPanel>());
    panels_.emplace_back(std::make_unique<FileBrowserPanel>(import_system));
}

EditorGUI::~EditorGUI() {
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void EditorGUI::UpdatePanels(const tryengine::core::Engine& engine) const {
    for (const auto& panel : panels_) {
        panel->OnUpdate(engine.GetClock().GetDeltaTime(), engine.GetInput(),
                        engine.GetSceneManager().GetActiveScene()->GetRegistry());
    }
}

void EditorGUI::RecordPanelsGpuCommands(const tryengine::core::Engine& engine) {
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    DrawDockSpace();

    for (const auto& panel : panels_) {
        panel->OnImGuiRender(engine.GetSceneManager().GetActiveScene()->GetRegistry());
    }
    // ImGui::ShowDemoWindow();

    ImGui::Render();
}

void EditorGUI::RenderToPanel(SDL_GPUCommandBuffer* cmd, tryengine::graphics::RenderSystem& render_system,
                              const tryengine::core::Engine& engine) const {
    for (const auto& panel : panels_) {
        panel->OnRender(cmd, render_system, engine.GetSceneManager().GetActiveScene()->GetRegistry());
    }
}

void EditorGUI::RenderPanelsToSwapchain(SDL_GPUTexture* swapchainTexture, SDL_GPUCommandBuffer* cmd) {
    ImDrawData* draw_data = ImGui::GetDrawData();
    ImGui_ImplSDLGPU3_PrepareDrawData(draw_data, cmd);

    SDL_GPUColorTargetInfo colorInfo{};
    colorInfo.texture = swapchainTexture;
    colorInfo.clear_color = {0, 0, 0, 1};
    colorInfo.load_op = SDL_GPU_LOADOP_CLEAR;
    colorInfo.store_op = SDL_GPU_STOREOP_STORE;

    const auto guiPass = SDL_BeginGPURenderPass(cmd, &colorInfo, 1, nullptr);

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

}  // namespace tryeditor
