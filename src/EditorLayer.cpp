#include "EditorLayer.hpp"

#include <ImGuizmo.h>

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <algorithm>
#include <glm/gtx/matrix_decompose.hpp>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_internal.h>

#include "Engine.hpp"
#include "EngineTypes.hpp"
#include "GraphicsContext.hpp"
#include "HierarchyPanel.hpp"
#include "InspectorPanel.hpp"
#include "Math.hpp"
#include "RenderTarget.hpp"

EditorLayer::EditorLayer(GraphicsContext& context) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    // Настройка бэккендов
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

EditorLayer::~EditorLayer() {
    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void EditorLayer::RecordRenderGUICommands(RenderTarget& renderTarget, entt::registry& reg, Engine& engine) {
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

void EditorLayer::DrawEngineControl(Engine& engine) {
    ImGui::Begin("Engine Control");

    // Массив строк для отображения в комбо-боксе
    const char* modes[] = {"Immediate (Uncapped)", "Mailbox (Fast)", "VSync (Locked)"};
    int currentItem = static_cast<int>(engine.settings.presentMode);

    if (ImGui::Combo("Sync Mode", &currentItem, modes, IM_ARRAYSIZE(modes))) {
        engine.settings.presentMode = static_cast<PresentMode>(currentItem);
        engine.PushCommand(CmdSetPresentMode(engine.settings.presentMode));
    }

    ImGui::Separator();
    ImGui::Text("FPS: %d", engine.time.currentFPS);

    // Подсказка для понимания разницы
    if (currentItem == 0) ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Warning: Screen tearing possible");

    ImGui::End();
}

void EditorLayer::DrawDockSpace() {
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

void EditorLayer::Update(Engine& engine, entt::registry& reg) {
    auto& input = engine.input;
    double deltaTime = engine.time.deltaTime;

    bool rightMouseDown = input.IsMouseButtonDown(MouseButton::Right);

    if (m_ViewportHovered && rightMouseDown && !m_IsCameraMoving) {
        m_IsCameraMoving = true;
        engine.PushCommand(CmdSetCursorCapture{true});

        // Блокируем ввод для ImGui
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
    } else if (!rightMouseDown && m_IsCameraMoving) {
        m_IsCameraMoving = false;
        engine.PushCommand(CmdSetCursorCapture{false});

        // Возвращаем ввод ImGui
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    }

    // if (m_ViewportHovered && input.IsMouseButtonDown(MouseButton::Left)) {
    //     PerformRaycast(reg, mX, mY, vW, vH);
    // }

    if (!m_IsCameraMoving) return;

    auto view = reg.view<TransformComponent, CameraComponent, EditorCameraTag>();
    auto entity = view.front();
    auto& transform = view.get<TransformComponent>(entity);
    auto& cam = view.get<CameraComponent>(entity);

    // Мы уже точно знаем, что курсор захвачен (или команда на захват уже отправлена)
    transform.rotation.y += input.mouseDeltaX * cam.sensitivity;  // Yaw
    transform.rotation.x -= input.mouseDeltaY * cam.sensitivity;  // Pitch
    transform.rotation.x = std::clamp(transform.rotation.x, -89.0f, 89.0f);

    glm::vec3 front;
    front.x = std::cos(glm::radians(transform.rotation.y)) * std::cos(glm::radians(transform.rotation.x));
    front.y = std::sin(glm::radians(transform.rotation.x));
    front.z = std::sin(glm::radians(transform.rotation.y)) * std::cos(glm::radians(transform.rotation.x));

    cam.front = glm::normalize(front);
    cam.right = glm::normalize(glm::cross(cam.front, glm::vec3(0, 1, 0)));
    cam.up = glm::normalize(glm::cross(cam.right, cam.front));

    float moveSpeed = cam.speed * static_cast<float>(deltaTime);

    if (input.keyboardState[SDL_SCANCODE_W]) transform.position += cam.front * moveSpeed;
    if (input.keyboardState[SDL_SCANCODE_S]) transform.position -= cam.front * moveSpeed;
    if (input.keyboardState[SDL_SCANCODE_A]) transform.position -= cam.right * moveSpeed;
    if (input.keyboardState[SDL_SCANCODE_D]) transform.position += cam.right * moveSpeed;
    if (input.keyboardState[SDL_SCANCODE_E]) transform.position += cam.up * moveSpeed;
    if (input.keyboardState[SDL_SCANCODE_Q]) transform.position -= cam.up * moveSpeed;

    cam.viewMatrix = glm::lookAt(transform.position, transform.position + cam.front, cam.up);
}
void EditorLayer::DrawSceneViewport(RenderTarget& renderTarget, entt::registry& reg) {
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

void EditorLayer::PerformRaycast(entt::registry& reg, float mX, float mY, float vW, float vH) {
    auto camView = reg.view<EditorCameraTag, CameraComponent, TransformComponent>();
    auto camEnt = camView.front();
    if (camEnt == entt::null) return;

    auto& camera = camView.get<CameraComponent>(camEnt);
    auto& camTransform = camView.get<TransformComponent>(camEnt);

    Ray ray = GetRayFromMouse(camera, camTransform, mX, mY, vW, vH);

    entt::entity selected = entt::null;
    float minT = std::numeric_limits<float>::max();

    auto pickView = reg.view<AABBComponent>();
    for (auto entity : pickView) {
        float t;
        if (RayIntersectsAABB(ray, pickView.get<AABBComponent>(entity), t)) {
            if (t < minT) {
                minT = t;
                selected = entity;
            }
        }
    }

    auto oldSelected = reg.view<SelectedTag>();
    reg.remove<SelectedTag>(oldSelected.begin(), oldSelected.end());

    if (selected != entt::null) {
        reg.emplace<SelectedTag>(selected);
    }
}

void EditorLayer::HandleGizmos(RenderTarget& renderTarget, entt::registry& reg) {
    auto view = reg.view<SelectedTag, TransformComponent, HierarchyComponent>();
    auto selectedE = view.front();
    if (selectedE == entt::null || !reg.valid(selectedE)) return;

    auto camView = reg.view<EditorCameraTag, CameraComponent>();
    auto camEnt = camView.front();
    if (camEnt == entt::null) return;

    auto& camera = camView.get<CameraComponent>(camEnt);
    auto& tc = reg.get<TransformComponent>(selectedE);
    auto& hc = reg.get<HierarchyComponent>(selectedE);

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

        if (hc.parent != entt::null && reg.all_of<TransformComponent>(hc.parent)) {
            auto& parentTC = reg.get<TransformComponent>(hc.parent);
            localMatrix = glm::inverse(parentTC.worldMatrix) * modelMatrix;
        }

        glm::vec3 skew;
        glm::vec4 perspective;
        glm::vec3 translation;
        glm::vec3 scale;
        glm::quat orientation;

        glm::decompose(localMatrix, scale, orientation, translation, skew, perspective);

        tc.position = translation;
        tc.rotation = glm::normalize(orientation);
        tc.scale = scale;
    }
}
