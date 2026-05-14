#include "editor/gui/EditorGUI.hpp"

#include <ImGuizmo.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include "editor/AddressablesProvider.hpp"
#include "editor/ControllerManager.hpp"
#include "editor/SceneManagerController.hpp"
#include "editor/gui/AddressablesPanel.hpp"
#include "editor/gui/FileBrowserPanel.hpp"
#include "editor/gui/GameViewportPanel.hpp"
#include "editor/gui/HierarchyPanel.hpp"
#include "editor/gui/InspectorPanel.hpp"
#include "editor/gui/SceneViewportPanel.hpp"
#include "engine/core/Clock.hpp"
#include "engine/core/Engine.hpp"
#include "engine/core/SceneManager.hpp"

namespace tryeditor {

EditorGUI::EditorGUI(tryengine::core::Engine& engine, tryengine::graphics::GraphicsContext& context,
                     ImportSystem& import_system, Spawner& spawner, SelectionManager& editor_context,
                     AssetsFactoryManager& factory_manager, AssetInspectorManager& inspector_manager,
                     AddressablesProvider& addressables_provider, ControllerManager& controller_manager)
    : engine_(engine), selection_manager_(editor_context), controller_manager_(controller_manager) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    static std::string ini_path = (std::filesystem::current_path() / "editor" / "imgui.ini").string();
    io.IniFilename = ini_path.c_str();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    ImGuizmo::Style& gizmoStyle = ImGuizmo::GetStyle();
    gizmoStyle.TranslationLineThickness = 4.0f;
    gizmoStyle.TranslationLineArrowSize = 6.0f;
    gizmoStyle.RotationLineThickness = 2.0f;
    gizmoStyle.RotationOuterLineThickness = 3.0f;
    gizmoStyle.ScaleLineThickness = 4.0f;
    gizmoStyle.ScaleLineCircleSize = 6.0f;
    gizmoStyle.HatchedAxisLineThickness = 6.0f;
    gizmoStyle.CenterCircleSize = 4.0f;

    ImGuizmo::SetGizmoSizeClipSpace(0.12f);

    // Можно даже переопределить цвета, чтобы сделать их ярче (Опционально)
    // style.Colors[ImGuizmo::DIRECTION_X] = ImVec4(0.9f, 0.2f, 0.2f, 1.0f); // Ярко-красный
    // style.Colors[ImGuizmo::DIRECTION_Y] = ImVec4(0.2f, 0.9f, 0.2f, 1.0f); // Ярко-зеленый
    // style.Colors[ImGuizmo::DIRECTION_Z] = ImVec4(0.2f, 0.2f, 0.9f, 1.0f); // Ярко-синий

    // Настройка бэкендов
    ImGui_ImplSDL3_InitForSDLGPU(context.GetWindow());

    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device = context.GetDevice();
    init_info.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(context.GetDevice(), context.GetWindow());
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;
    init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    ImGui_ImplSDLGPU3_Init(&init_info);

    panels_.emplace_back(std::make_unique<SceneViewportPanel>(context, spawner));
    panels_.emplace_back(std::make_unique<GameViewportPanel>(context));
    panels_.emplace_back(
        std::make_unique<InspectorPanel>(editor_context, import_system, inspector_manager, addressables_provider));
    panels_.emplace_back(std::make_unique<HierarchyPanel>(selection_manager_));
    panels_.emplace_back(std::make_unique<FileBrowserPanel>(import_system, editor_context, factory_manager));
    panels_.emplace_back(std::make_unique<AddressablesPanel>(addressables_provider));
}

EditorGUI::~EditorGUI() {
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void EditorGUI::UpdatePanels(const tryengine::core::Engine& engine) const {
    for (const auto& panel : panels_) {
        panel->OnUpdate(engine.GetClock().GetDeltaTime(), engine.GetInput(),
                        engine.GetSceneManager().GetActiveScene().GetRegistry());
    }
}

void EditorGUI::RecordPanelsGpuCommands(const tryengine::core::Engine& engine, bool& is_playing) {
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    // 1. Сначала меню
    DrawMainMenu();
    // 2. Затем панель кнопок
    DrawPlayToolbar(is_playing);
    // 3. И только потом докспейс
    DrawDockSpace();

    for (const auto& panel : panels_) {
        panel->OnImGuiRender(engine.GetSceneManager().GetActiveScene().GetRegistry());
    }

    ImGui::Render();
}

void EditorGUI::RenderToPanel(SDL_GPUCommandBuffer* cmd, tryengine::graphics::RenderSystem& render_system,
                              const tryengine::core::Engine& engine) const {
    for (const auto& panel : panels_) {
        panel->OnRender(cmd, render_system, engine.GetSceneManager().GetActiveScene().GetRegistry());
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

    float toolbar_height = 30.0f;  // Должно совпадать с высотой из DrawPlayToolbar

    // Сдвигаем начало DockSpace на высоту тулбара и уменьшаем его общий размер
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

void EditorGUI::DrawMainMenu() {
    // Делаем фон меню таким же темным, как и фон обычных окон
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Scene")) {
                controller_manager_.Get<SceneManagerController>().SaveScene();
            }
            if (ImGui::MenuItem("Save Scene As")) {
                controller_manager_.Get<SceneManagerController>().SaveScene();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit")) {
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Asset")) {
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    ImGui::PopStyleColor();
}

void EditorGUI::DrawPlayToolbar(bool& is_playing) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Задаем жесткую высоту панели
    float toolbar_height = 30.0f;

    // Позиция WorkPos автоматически учитывает отступ от MainMenuBar,
    // поэтому панель встанет ровно под вкладками File/Edit
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, toolbar_height));
    ImGui::SetNextWindowViewport(viewport->ID);

    // Флаги, запрещающие изменение размера, перемещение, докинг и скрывающие декорации
    ImGuiWindowFlags toolbar_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(0.0f, 8.0f));  // Отступы для центрирования кнопок по вертикали
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyle().Colors[ImGuiCol_WindowBg]);

    ImGui::Begin("PlayToolbar", nullptr, toolbar_flags);

    // Центрируем кнопки по горизонтали
    float button_area_width = 120.0f;  // Примерная ширина двух кнопок с отступом
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - button_area_width) * 0.5f);

    if (ImGui::Button(is_playing ? "Stop" : "Play", ImVec2(50, 0))) {
        is_playing = !is_playing;
    }
    ImGui::SameLine();
    if (ImGui::Button("Pause", ImVec2(50, 0))) {
        // Логика паузы
    }

    ImGui::End();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}
}  // namespace tryeditor
