#pragma once

#include <imgui.h>

#include <ImGuizmo.h>

#include <entt/entt.hpp>
#include <string>

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

    template <typename T>
    void DrawAddComponentEntry(const std::string& entryName, entt::registry& reg);

    template <typename T, typename UIFunction>
    void DrawComponent(const std::string& name, entt::registry& reg, UIFunction uiFunction);

    void DrawEntityNode(entt::entity entity, entt::registry& reg, entt::entity& entityToDestroy);

    // --- НОВЫЕ МЕТОДЫ И ПЕРЕМЕННЫЕ ---
    void HandleHierarchyShortcuts(entt::registry& reg, entt::entity& entityToDestroy);
    bool IsDescendantOf(entt::entity child, entt::entity parent, entt::registry& reg);
    void ReparentEntity(entt::entity child, entt::entity newParent, entt::registry& reg);
    entt::entity CloneEntity(entt::entity srcEntity, entt::registry& reg);

    entt::entity m_SelectedEntity = entt::null;

    // Буфер обмена
    entt::entity m_ClipboardEntity = entt::null;
    bool m_IsCutOperation = false;

    // Переименование
    entt::entity m_EntityToRename = entt::null;
    char m_RenameBuffer[256] = "";

    ImGuizmo::OPERATION m_CurrentGizmoOperation = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE m_CurrentGizmoMode = ImGuizmo::LOCAL;
};
