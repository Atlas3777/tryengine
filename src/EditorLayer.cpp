#include "EditorLayer.hpp"

#include <ImGuizmo.h>

#include <glm/gtc/type_ptr.hpp>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include "Engine.hpp"
#include "EngineTypes.hpp"
#include "GraphicsContext.hpp"
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
    DrawHierarchy(reg);
    DrawInspector(reg);
    DrawEngineControl(engine);

    ImGui::ShowDemoWindow();

    // 4. Финализация
    ImGui::Render();
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

void EditorLayer::DrawSceneViewport(RenderTarget& renderTarget, entt::registry& reg) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Scene");

    ImVec2 viewportSize = ImGui::GetContentRegionAvail();

    if (viewportSize.x > 0 && viewportSize.y > 0) {
        // Ресайз рендер-таргета под размер окна
        if ((uint32_t)viewportSize.x != renderTarget.GetWidth() ||
            (uint32_t)viewportSize.y != renderTarget.GetHeight()) {
            renderTarget.Resize((uint32_t)viewportSize.x, (uint32_t)viewportSize.y);
        }
        ImGui::Image((ImTextureID)renderTarget.GetColor(), viewportSize);
    }

    // Рисуем гизмо прямо поверх окна вьюпорта
    HandleGizmos(renderTarget, reg);

    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorLayer::HandleGizmos(RenderTarget& renderTarget, entt::registry& reg) {
    if (m_SelectedEntity == entt::null || !reg.valid(m_SelectedEntity)) return;

    auto camView = reg.view<EditorCameraTag, CameraComponent>();
    auto camEnt = camView.front();
    if (camEnt == entt::null) return;

    auto& camera = camView.get<CameraComponent>(camEnt);

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowWidth(),
                      ImGui::GetWindowHeight());

    glm::mat4 viewMat = camera.viewMatrix;
    float aspect = (float)renderTarget.GetWidth() / (float)renderTarget.GetHeight();
    glm::mat4 projMat = glm::perspective(glm::radians(camera.fov), aspect, camera.nearPlane, camera.farPlane);

    auto& tc = reg.get<TransformComponent>(m_SelectedEntity);
    glm::mat4 modelMat = tc.GetLocalMatrix();

    // Сетка (пока оставляем так, но лучше вынести в рендер)
    // ImGuizmo::DrawGrid(glm::value_ptr(viewMat), glm::value_ptr(projMat), glm::value_ptr(glm::mat4(1.0f)), 20.f);

    ImGuizmo::Manipulate(glm::value_ptr(viewMat), glm::value_ptr(projMat), m_CurrentGizmoOperation, m_CurrentGizmoMode,
                         glm::value_ptr(modelMat));

    if (ImGuizmo::IsUsing()) {
        float mPos[3], mRot[3], mScale[3];
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(modelMat), mPos, mRot, mScale);

        tc.position = glm::make_vec3(mPos);
        tc.rotation = glm::make_vec3(mRot);
        tc.scale = glm::make_vec3(mScale);
    }
}

void EditorLayer::DrawHierarchy(entt::registry& reg) {
    ImGui::Begin("Hierarchy");

    // Контекстное меню для пустого пространства окна (создание сущностей)
    if (ImGui::BeginPopupContextWindow("HierarchyContextMenu",
                                       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Create Empty Entity")) {
            auto newEntity = reg.create();
            reg.emplace<TransformComponent>(newEntity);  // По умолчанию даем трансформ
            m_SelectedEntity = newEntity;
        }
        ImGui::EndPopup();
    }

    entt::entity entityToDestroy = entt::null;
    auto view = reg.view<TransformComponent>();  // Или reg.storage<entt::entity>() если хочешь вообще все сущности

    for (auto entity : view) {
        ImGuiTreeNodeFlags flags = ((m_SelectedEntity == entity) ? ImGuiTreeNodeFlags_Selected : 0);
        flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

        // В будущем здесь можно брать NameComponent
        std::string label = "Entity [" + std::to_string(static_cast<uint32_t>(entity)) + "]";
        bool opened = ImGui::TreeNodeEx((void*)(uintptr_t)entity, flags, "%s", label.c_str());

        if (ImGui::IsItemClicked()) {
            m_SelectedEntity = entity;
        }

        // Контекстное меню для конкретной сущности (удаление)
        bool entityDeleted = false;
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Destroy Entity")) {
                entityDeleted = true;
            }
            ImGui::EndPopup();
        }

        if (opened) {
            ImGui::TreePop();
        }

        if (entityDeleted) {
            entityToDestroy = entity;
        }
    }

    // Сброс выделения
    if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
        m_SelectedEntity = entt::null;
    }

    // Обработка удаления вне цикла (чтобы не сломать итератор EnTT)
    if (entityToDestroy != entt::null) {
        if (m_SelectedEntity == entityToDestroy) m_SelectedEntity = entt::null;
        reg.destroy(entityToDestroy);
    }

    ImGui::End();
}

void EditorLayer::DrawInspector(entt::registry& reg) {
    ImGui::Begin("Inspector");

    if (m_SelectedEntity != entt::null && reg.valid(m_SelectedEntity)) {
        ImGui::TextDisabled("Entity ID: %d", static_cast<uint32_t>(m_SelectedEntity));
        ImGui::Separator();
        ImGui::Spacing();

        // --- Отрисовка компонентов через шаблоны ---

        DrawComponent<TransformComponent>("Transform", reg, [](auto& tc) {
            ImGui::DragFloat3("Position", glm::value_ptr(tc.position), 0.1f);
            ImGui::DragFloat3("Rotation", glm::value_ptr(tc.rotation), 1.0f);
            ImGui::DragFloat3("Scale", glm::value_ptr(tc.scale), 0.05f);
        });

        DrawComponent<CameraComponent>("Camera", reg, [](auto& cam) {
            ImGui::DragFloat("FOV", &cam.fov, 1.0f);
            ImGui::DragFloat("Near Plane", &cam.nearPlane, 0.1f);
            ImGui::DragFloat("Far Plane", &cam.farPlane, 1.0f);
            ImGui::DragFloat("Speed", &cam.speed, 0.1f);
            // Чекбокс для MainCamera можно тоже вынести сюда, если логика позволяет
        });

        DrawComponent<MeshComponent>("Mesh Renderer", reg, [](auto& mc) {
            ImGui::Text("Mesh pointer: %p", mc.mesh);
            // В будущем тут будет Dropdown или Drag-n-Drop для выбора меша
        });

        // --- Кнопка добавления компонентов ---
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float width = ImGui::GetContentRegionAvail().x;
        float buttonWidth = 150.0f;
        ImGui::SetCursorPosX((width - buttonWidth) * 0.5f);

        if (ImGui::Button("Add Component", ImVec2(buttonWidth, 0))) {
            ImGui::OpenPopup("AddComponentPopup");
        }

        if (ImGui::BeginPopup("AddComponentPopup")) {
            DrawAddComponentMenu(reg);
            ImGui::EndPopup();
        }
    } else {
        ImGui::TextDisabled("Select an entity to inspect.");
    }

    ImGui::End();
}

template <typename T>
void EditorLayer::DrawAddComponentEntry(const std::string& entryName, entt::registry& reg) {
    if (!reg.all_of<T>(m_SelectedEntity)) {
        if (ImGui::Selectable(entryName.c_str())) {
            reg.emplace<T>(m_SelectedEntity);
            ImGui::CloseCurrentPopup();
        }
    }
}

void EditorLayer::DrawAddComponentMenu(entt::registry& reg) {
    // В будущем здесь можно добавить ImGui::InputText для поиска (фильтрации списка)

    DrawAddComponentEntry<CameraComponent>("Camera", reg);

    // Для MeshComponent нужна специализация, если мы хотим передавать дефолтные параметры (nullptr)
    if (!reg.all_of<MeshComponent>(m_SelectedEntity)) {
        if (ImGui::Selectable("Mesh Renderer")) {
            reg.emplace<MeshComponent>(m_SelectedEntity, nullptr);
            ImGui::CloseCurrentPopup();
        }
    }

    // Будущие компоненты добавляются одной строкой:
    // DrawAddComponentEntry<LightComponent>("Light Source", reg);
    // DrawAddComponentEntry<RigidBodyComponent>("Rigid Body", reg);
}

void EditorLayer::DrawEngineControl(Engine& engine) {
    ImGui::Begin("Engine Control");
    if (ImGui::Checkbox("VSync Enable", &engine.settings.vSyncEnable)) {
        engine.PushCommand(CmdSetVSync(engine.settings.vSyncEnable));
    }
    ImGui::Text("FPS: %d", engine.time.currentFPS);
    ImGui::End();
}

template <typename T, typename UIFunction>
void EditorLayer::DrawComponent(const std::string& name, entt::registry& reg, UIFunction uiFunction) {
    // Проверяем, есть ли компонент у выбранной сущности
    if (!reg.all_of<T>(m_SelectedEntity)) return;

    // Стилизация хедера компонента
    const ImGuiTreeNodeFlags treeNodeFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed |
                                             ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_AllowOverlap |
                                             ImGuiTreeNodeFlags_FramePadding;

    // Используем хэш типа в качестве ID, чтобы ImGui не путал компоненты с одинаковыми именами (если такие будут)
    ImGui::PushID((void*)typeid(T).hash_code());

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{4, 4});
    bool open = ImGui::TreeNodeEx((void*)typeid(T).hash_code(), treeNodeFlags, "%s", name.c_str());
    ImGui::PopStyleVar();

    // Контекстное меню на хедере компонента (Правый клик по заголовку)
    bool removeComponent = false;
    if (ImGui::BeginPopupContextItem("ComponentSettings")) {
        if (ImGui::MenuItem("Remove Component")) {
            removeComponent = true;
        }
        ImGui::EndPopup();
    }

    if (open) {
        auto& component = reg.get<T>(m_SelectedEntity);
        uiFunction(component);  // Вызов лямбды с UI-логикой
        ImGui::TreePop();
    }

    // Удаляем компонент безопасно
    if (removeComponent) {
        reg.remove<T>(m_SelectedEntity);
    }

    ImGui::PopID();
}
