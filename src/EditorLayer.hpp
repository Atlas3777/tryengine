#pragma once

#include <imgui.h>

#include <ImGuizmo.h>

#include <entt/entt.hpp>
#include <string>

class HierarchyPanel;
class InspectorPanel;
class Engine;
class GraphicsContext;
class RenderTarget;

class EditorLayer {
   public:
    EditorLayer(GraphicsContext& context);
    ~EditorLayer();

    void RecordRenderGUICommands(RenderTarget& renderTarget, entt::registry& reg, Engine& engine);

   private:
    std::unique_ptr<HierarchyPanel> hierarchy;
    std::unique_ptr<InspectorPanel> inspector;
    void DrawDockSpace();
    void DrawSceneViewport(RenderTarget& renderTarget, entt::registry& reg);
    void DrawEngineControl(Engine& engine);
    void DrawAddComponentButton(entt::registry& reg);

    void HandleGizmos(RenderTarget& renderTarget, entt::registry& reg);

    ImGuizmo::OPERATION m_CurrentGizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE m_CurrentGizmoMode = ImGuizmo::LOCAL;
};
