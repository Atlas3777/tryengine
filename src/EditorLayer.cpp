#include "EditorLayer.hpp"

#include <ImGuizmo.h>

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_internal.h>

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
    auto& tc = reg.get<TransformComponent>(m_SelectedEntity);
    auto& hc = reg.get<HierarchyComponent>(m_SelectedEntity);

    // 1. Настройка пространства
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y, ImGui::GetWindowWidth(),
                      ImGui::GetWindowHeight());

    // Матрицы камеры
    glm::mat4 viewMat = camera.viewMatrix;
    float aspect = (float)renderTarget.GetWidth() / (float)renderTarget.GetHeight();
    glm::mat4 projMat = glm::perspective(glm::radians(camera.fov), aspect, camera.nearPlane, camera.farPlane);

    // 2. РАБОТАЕМ С МИРОВОЙ МАТРИЦЕЙ
    // Если мы в иерархии, нам нужна актуальная мировая матрица для ImGuizmo
    glm::mat4 modelMatrix = tc.worldMatrix;

    // ВАЖНО: ImGuizmo лучше работает, когда матрица "чистая".
    // Иногда стоит пересобрать её из компонентов прямо перед Manipulate, если worldMatrix запаздывает на кадр

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
void EditorLayer::DrawInspector(entt::registry& reg) {
    ImGui::Begin("Inspector");

    if (m_SelectedEntity != entt::null && reg.valid(m_SelectedEntity)) {
        ImGui::TextDisabled("Entity ID: %d", static_cast<uint32_t>(m_SelectedEntity));
        ImGui::Separator();
        ImGui::Spacing();

        // --- Отрисовка компонентов через шаблоны ---

        DrawComponent<TransformComponent>("Transform", reg, [](auto& tc) {
            ImGui::DragFloat3("Position", glm::value_ptr(tc.position), 0.1f);

            glm::vec3 euler = glm::degrees(glm::eulerAngles(tc.rotation));
            if (ImGui::DragFloat3("Rotation", glm::value_ptr(euler), 1.0f)) {
                tc.rotation = glm::normalize(glm::quat(glm::radians(euler)));
            }

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
void EditorLayer::DrawEntityNode(entt::entity entity, entt::registry& reg, entt::entity& entityToDestroy) {
    ImGuiTreeNodeFlags flags = ((m_SelectedEntity == entity) ? ImGuiTreeNodeFlags_Selected : 0);
    flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

    bool hasChildren = false;
    auto hierarchyView = reg.view<HierarchyComponent>();
    for (auto child : hierarchyView) {
        if (hierarchyView.get<HierarchyComponent>(child).parent == entity) {
            hasChildren = true;
            break;
        }
    }

    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

    // Получаем имя сущности
    std::string label = "Entity [" + std::to_string(static_cast<uint32_t>(entity)) + "]";
    if (reg.all_of<TagComponent>(entity)) {
        label = reg.get<TagComponent>(entity).tag;
    }

    // Обработка переименования (R)
    if (m_EntityToRename == entity) {
        flags &= ~ImGuiTreeNodeFlags_SpanAvailWidth;  // Убираем, чтобы InputText нормально встал
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            if (!reg.all_of<TagComponent>(entity)) reg.emplace<TagComponent>(entity);
            reg.get<TagComponent>(entity).tag = m_RenameBuffer;
            m_EntityToRename = entt::null;
        }
        if (ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_EntityToRename = entt::null;  // Отмена по ESC
        }
    } else {
        // Обычная отрисовка узла
        bool opened = ImGui::TreeNodeEx((void*)(uintptr_t)entity, flags, "%s", label.c_str());

        // Выделение
        if (ImGui::IsItemClicked()) {
            m_SelectedEntity = entity;
        }

        // --- Drag & Drop: Источник ---
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &entity, sizeof(entt::entity));
            ImGui::Text("Move %s", label.c_str());
            ImGui::EndDragDropSource();
        }

        // --- Drag & Drop: Цель ---
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
                entt::entity droppedEntity = *(entt::entity*)payload->Data;
                ReparentEntity(droppedEntity, entity, reg);
            }
            ImGui::EndDragDropTarget();
        }

        // Контекстное меню
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Destroy Entity")) entityToDestroy = entity;
            ImGui::EndPopup();
        }

        // Рекурсия детей
        if (opened) {
            if (hasChildren) {
                for (auto child : hierarchyView) {
                    if (hierarchyView.get<HierarchyComponent>(child).parent == entity) {
                        DrawEntityNode(child, reg, entityToDestroy);
                    }
                }
            }
            ImGui::TreePop();
        }
    }
}

void EditorLayer::DrawHierarchy(entt::registry& reg) {
    ImGui::Begin("Hierarchy");

    entt::entity entityToDestroy = entt::null;

    // --- Обработка горячих клавиш ---
    HandleHierarchyShortcuts(reg, entityToDestroy);

    if (ImGui::BeginPopupContextWindow("HierarchyContextMenu",
                                       ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (ImGui::MenuItem("Create Empty Entity")) {
            auto newEntity = reg.create();
            reg.emplace<TransformComponent>(newEntity);
            reg.emplace<HierarchyComponent>(newEntity);  // Даем базовую иерархию
            reg.emplace<TagComponent>(newEntity, "New Entity");
            m_SelectedEntity = newEntity;
        }
        ImGui::EndPopup();
    }

    // Если перетаскиваем сущность в пустое место окна (сброс родителя в root)
    if (ImGui::BeginDragDropTargetCustom(ImGui::GetCurrentWindow()->Rect(), ImGui::GetID("Hierarchy"))) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY")) {
            entt::entity droppedEntity = *(entt::entity*)payload->Data;
            ReparentEntity(droppedEntity, entt::null, reg);
        }
        ImGui::EndDragDropTarget();
    }

    // Отрисовка
    for (auto [entity] : reg.storage<entt::entity>().each()) {
        bool isRoot = true;
        if (reg.all_of<HierarchyComponent>(entity)) {
            if (reg.get<HierarchyComponent>(entity).parent != entt::null) {
                isRoot = false;
            }
        }
        if (isRoot) {
            DrawEntityNode(entity, reg, entityToDestroy);
        }
    }

    if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered()) {
        m_SelectedEntity = entt::null;
    }

    if (entityToDestroy != entt::null) {
        if (m_SelectedEntity == entityToDestroy) m_SelectedEntity = entt::null;
        reg.destroy(entityToDestroy);  // Позже стоит сделать рекурсивным
    }

    ImGui::End();
}

void EditorLayer::HandleHierarchyShortcuts(entt::registry& reg, entt::entity& entityToDestroy) {
    // Выполняем только если окно иерархии в фокусе и мы не печатаем текст
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::IsAnyItemActive()) return;

    ImGuiIO& io = ImGui::GetIO();

    // HJKL Навигация (Эмуляция нажатия стрелочек для ImGui TreeNode)
    if (ImGui::IsKeyPressed(ImGuiKey_J)) io.AddKeyEvent(ImGuiKey_DownArrow, true);
    if (ImGui::IsKeyReleased(ImGuiKey_J)) io.AddKeyEvent(ImGuiKey_DownArrow, false);

    if (ImGui::IsKeyPressed(ImGuiKey_K)) io.AddKeyEvent(ImGuiKey_UpArrow, true);
    if (ImGui::IsKeyReleased(ImGuiKey_K)) io.AddKeyEvent(ImGuiKey_UpArrow, false);

    if (ImGui::IsKeyPressed(ImGuiKey_L)) io.AddKeyEvent(ImGuiKey_RightArrow, true);
    if (ImGui::IsKeyReleased(ImGuiKey_L)) io.AddKeyEvent(ImGuiKey_RightArrow, false);

    if (ImGui::IsKeyPressed(ImGuiKey_H)) io.AddKeyEvent(ImGuiKey_LeftArrow, true);
    if (ImGui::IsKeyReleased(ImGuiKey_H)) io.AddKeyEvent(ImGuiKey_LeftArrow, false);

    // Действия над выделенной сущностью
    if (m_SelectedEntity != entt::null && reg.valid(m_SelectedEntity)) {
        // УДАЛЕНИЕ (D)
        if (ImGui::IsKeyPressed(ImGuiKey_D)) {
            entityToDestroy = m_SelectedEntity;
        }
        // ПЕРЕИМЕНОВАНИЕ (R)
        if (ImGui::IsKeyPressed(ImGuiKey_R)) {
            m_EntityToRename = m_SelectedEntity;
            std::string currentName =
                reg.all_of<TagComponent>(m_SelectedEntity) ? reg.get<TagComponent>(m_SelectedEntity).tag : "";
            strncpy(m_RenameBuffer, currentName.c_str(), sizeof(m_RenameBuffer));
        }
        // ВЫРЕЗАТЬ (X)
        if (ImGui::IsKeyPressed(ImGuiKey_X)) {
            m_ClipboardEntity = m_SelectedEntity;
            m_IsCutOperation = true;
        }
        // КОПИРОВАТЬ (Y)
        if (ImGui::IsKeyPressed(ImGuiKey_Y)) {
            m_ClipboardEntity = m_SelectedEntity;
            m_IsCutOperation = false;
        }
    }

    // ВСТАВКА (P) - работает даже если ничего не выделено (в корень)
    if (ImGui::IsKeyPressed(ImGuiKey_P) && m_ClipboardEntity != entt::null && reg.valid(m_ClipboardEntity)) {
        if (m_IsCutOperation) {
            // Перемещение
            ReparentEntity(m_ClipboardEntity, m_SelectedEntity, reg);
            m_ClipboardEntity = entt::null;  // Сбрасываем буфер после Cut->Paste
        } else {
            // Клонирование
            entt::entity newEntity = CloneEntity(m_ClipboardEntity, reg);
            ReparentEntity(newEntity, m_SelectedEntity, reg);
        }
    }
}

// Защита от циклических зависимостей (нельзя кинуть деда в внука)
bool EditorLayer::IsDescendantOf(entt::entity child, entt::entity parent, entt::registry& reg) {
    if (child == parent) return true;
    if (!reg.all_of<HierarchyComponent>(child)) return false;

    entt::entity curr = reg.get<HierarchyComponent>(child).parent;
    while (curr != entt::null && reg.valid(curr)) {
        if (curr == parent) return true;
        if (!reg.all_of<HierarchyComponent>(curr)) break;
        curr = reg.get<HierarchyComponent>(curr).parent;
    }
    return false;
}

void EditorLayer::ReparentEntity(entt::entity child, entt::entity newParent, entt::registry& reg) {
    if (child == newParent) return;                                                // Нельзя вложить в себя
    if (newParent != entt::null && IsDescendantOf(newParent, child, reg)) return;  // Нельзя вложить в своего потомка

    if (!reg.all_of<HierarchyComponent>(child)) {
        reg.emplace<HierarchyComponent>(child);
    }

    auto& hc = reg.get<HierarchyComponent>(child);
    hc.parent = newParent;

    // Опционально: здесь нужно пересчитать глубину (depth), если ты используешь ее для сортировки систем
    hc.depth = 0;
    if (newParent != entt::null && reg.all_of<HierarchyComponent>(newParent)) {
        hc.depth = reg.get<HierarchyComponent>(newParent).depth + 1;
    }
}

// Ручное клонирование компонентов (так как в C++ нет рефлексии)
entt::entity EditorLayer::CloneEntity(entt::entity srcEntity, entt::registry& reg) {
    entt::entity dest = reg.create();

    if (reg.all_of<TagComponent>(srcEntity)) {
        reg.emplace<TagComponent>(dest, reg.get<TagComponent>(srcEntity).tag + " (Copy)");
    }
    if (reg.all_of<TransformComponent>(srcEntity)) {
        reg.emplace<TransformComponent>(dest, reg.get<TransformComponent>(srcEntity));
    }
    if (reg.all_of<HierarchyComponent>(srcEntity)) {
        auto& srcHc = reg.get<HierarchyComponent>(srcEntity);
        reg.emplace<HierarchyComponent>(dest, srcHc.parent, srcHc.depth);
    }
    if (reg.all_of<MeshComponent>(srcEntity)) {
        reg.emplace<MeshComponent>(dest, reg.get<MeshComponent>(srcEntity));
    }
    if (reg.all_of<CameraComponent>(srcEntity)) {
        reg.emplace<CameraComponent>(dest, reg.get<CameraComponent>(srcEntity));
    }

    // Внимание: клонирование детей здесь не реализовано.
    // Для глубокого копирования (Deep Copy) потребуется рекурсивно искать потомков srcEntity и клонировать их тоже.
    return dest;
}
