#include "editor/EditorGUI.hpp"

#include <ImGuizmo.h>

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <algorithm>
#include <glm/gtx/matrix_decompose.hpp>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_internal.h>

#include "../../engine/core/include/engine/core/Components.hpp"
#include "../../engine/core/include/engine/core/Engine.hpp"
#include "../../engine/graphics/include/engine/graphics/RenderTarget.hpp"
#include "editor/Components.hpp"
#include "editor/HierarchyPanel.hpp"
#include "editor/InspectorPanel.hpp"

namespace editor {
using namespace engine::core;
using namespace engine;

EditorGUI::EditorGUI(engine::graphics::GraphicsContext& context) {
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

    inspector = std::make_unique<InspectorPanel>();
    hierarchy = std::make_unique<HierarchyPanel>();
}

EditorGUI::~EditorGUI() {
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void EditorGUI::RecordRenderGUICommands(engine::graphics::RenderTarget& renderTarget, entt::registry& reg,
                                        engine::core::Engine& engine) {
    // 1. Начало кадра ImGui
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    // 2. Глобальные хоткеи (W, E, R...)
    if (!ImGui::IsAnyItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_W)) m_CurrentGizmoOperation = ImGuizmo::TRANSLATE;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) m_CurrentGizmoOperation = ImGuizmo::ROTATE;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) m_CurrentGizmoOperation = ImGuizmo::SCALE;
    }

    // 3. Отрисовка окон
    DrawDockSpace();

    DrawSceneViewport(renderTarget, reg);

    hierarchy.get()->OnImGuiRender(reg);
    inspector.get()->OnImGuiRender(reg);

    DrawEngineControl(engine);

    ImGui::ShowDemoWindow();

    // 4. Финализация
    ImGui::Render();
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

void EditorGUI::DrawEngineControl(engine::core::Engine& engine) {
    // ImGui::Begin("Engine Control");
    //
    // // Массив строк для отображения в комбо-боксе
    // const char* modes[] = {"Immediate (Uncapped)", "Mailbox (Fast)", "VSync (Locked)"};
    // int currentItem = static_cast<int>(engine.settings.presentMode);
    //
    // if (ImGui::Combo("Sync Mode", &currentItem, modes, IM_ARRAYSIZE(modes))) {
    //     engine.settings.presentMode = static_cast<engine::PresentMode>(currentItem);
    //     engine.PushCommand(engine::CmdSetPresentMode(engine.settings.presentMode));
    // }
    //
    // ImGui::Separator();
    // ImGui::Text("FPS: %d", engine.time.currentFPS);
    //
    // // Подсказка для понимания разницы
    // if (currentItem == 0) ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Warning: Screen tearing possible");
    //
    // ImGui::End();
}

void EditorGUI::DrawSceneViewport(engine::graphics::RenderTarget& renderTarget, entt::registry& reg) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Scene");

    m_ViewportHovered = ImGui::IsWindowHovered();
    ImVec2 viewportSize = ImGui::GetContentRegionAvail();

    if (viewportSize.x > 0 && viewportSize.y > 0) {
        if ((uint32_t)viewportSize.x != renderTarget.GetWidth() ||
            (uint32_t)viewportSize.y != renderTarget.GetHeight()) {
            renderTarget.Resize((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);
        }
        ImGui::Image((ImTextureID)renderTarget.GetColor(), viewportSize);
    }

    HandleGizmos(renderTarget, reg);

    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorGUI::HandleGizmos(engine::graphics::RenderTarget& renderTarget, entt::registry& reg) {
    auto view = reg.view<SelectedTag, engine::Transform, engine::Hierarchy>();
    auto selectedE = view.front();
    if (selectedE == entt::null || !reg.valid(selectedE)) return;

    auto camView = reg.view<EditorCameraTag, engine::Camera>();
    auto camEnt = camView.front();
    if (camEnt == entt::null) return;

    auto& camera = camView.get<engine::Camera>(camEnt);
    auto& tc = reg.get<engine::Transform>(selectedE);
    auto& hc = reg.get<engine::Hierarchy>(selectedE);

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowWidth(),
                      ImGui::GetWindowHeight());

    glm::mat4 viewMat = camera.viewMatrix;
    float aspect = (float)renderTarget.GetWidth() / (float)renderTarget.GetHeight();
    glm::mat4 projMat = glm::perspective(glm::radians(camera.fov), aspect, camera.nearPlane, camera.farPlane);

    glm::mat4 modelMatrix = tc.worldMatrix;

    ImGuizmo::Manipulate(glm::value_ptr(viewMat), glm::value_ptr(projMat), m_CurrentGizmoOperation, m_CurrentGizmoMode,
                         glm::value_ptr(modelMatrix));

    if (ImGuizmo::IsUsing()) {
        glm::mat4 localMatrix = modelMatrix;

        if (hc.parent != entt::null && reg.all_of<Transform>(hc.parent)) {
            auto& parentTC = reg.get<Transform>(hc.parent);
            localMatrix = glm::inverse(parentTC.worldMatrix) * modelMatrix;
        }

        vec3 skew;
        vec4 perspective;
        vec3 translation;
        vec3 scale;
        glm::quat orientation;

        glm::decompose(localMatrix, scale, orientation, translation, skew, perspective);

        tc.position = translation;
        tc.rotation = glm::normalize(orientation);
        tc.scale = scale;
    }
}
}  // namespace editor
