#pragma once

#include <imgui.h>

#include <ImGuizmo.h>

#include <entt/entt.hpp>

#include "../../../engine/core/include/engine/core/Engine.hpp"
#include "../../../engine/graphics/include/engine/graphics/GraphicsContext.hpp"
#include "../../../engine/graphics/include/engine/graphics/RenderTarget.hpp"
#include "editor/HierarchyPanel.hpp"
#include "editor/InspectorPanel.hpp"

namespace editor {
class EditorGUI {
   public:
    EditorGUI(engine::GraphicsContext& context);
    ~EditorGUI();

    void RecordRenderGUICommands(engine::RenderTarget& renderTarget, entt::registry& reg, engine::Engine& engine);

    //void Update(engine::Engine& engine, entt::registry& reg);

   private:
    std::unique_ptr<HierarchyPanel> hierarchy;
    std::unique_ptr<InspectorPanel> inspector;
    void DrawDockSpace();
    void DrawSceneViewport(engine::RenderTarget& renderTarget, entt::registry& reg);
    void DrawEngineControl(engine::Engine& engine);
    void DrawAddComponentButton(entt::registry& reg);

    void HandleGizmos(engine::RenderTarget& renderTarget, entt::registry& reg);
    //void PerformRaycast(entt::registry& reg, float mX, float mY, float vW, float vH);

    float m_StoredMouseX = 0.0f, m_StoredMouseY = 0.0f;
    bool m_ViewportHovered = false;
    bool m_IsCameraMoving = false;
    ImGuizmo::OPERATION m_CurrentGizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE m_CurrentGizmoMode = ImGuizmo::LOCAL;
};
}  // namespace editor
