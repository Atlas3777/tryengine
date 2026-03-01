#pragma once

#include <imgui.h>

#include <ImGuizmo.h>  // Оставляем, так как используются enum (OPERATION и MODE)

#include <entt/entt.hpp>  // EnTT лучше оставить (из-за entt::entity и entt::null)

// Forward declarations
class Engine;
class GraphicsContext;
class RenderTarget;

class EditorLayer {
   public:
    EditorLayer(GraphicsContext& context);
    ~EditorLayer();

    void RecordRenderGUICommands(RenderTarget& renderTarget, entt::registry& reg, Engine& engine);

   private:
    void DrawDockSpace();
    void DrawSceneViewport(RenderTarget& renderTarget, entt::registry& reg);
    void DrawHierarchy(entt::registry& reg);
    void DrawInspector(entt::registry& reg);
    void DrawEngineControl(Engine& engine);
    void DrawAddComponentMenu(entt::registry& reg);

    void HandleGizmos(RenderTarget& renderTarget, entt::registry& reg);

    // Шаблонный хелпер для добавления компонентов в меню
    template <typename T>
    void DrawAddComponentEntry(const std::string& entryName, entt::registry& reg);

    // Универсальный метод отрисовки компонента с контекстным меню для удаления
    template <typename T, typename UIFunction>
    void DrawComponent(const std::string& name, entt::registry& reg, UIFunction uiFunction);

   private:
    entt::entity m_SelectedEntity = entt::null;
    ImGuizmo::OPERATION m_CurrentGizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE m_CurrentGizmoMode = ImGuizmo::LOCAL;
};
