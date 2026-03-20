#pragma once

#include <imgui.h>

#include <entt/entity/entity.hpp>
#include <vector>

namespace editor {
class HierarchyPanel {
   public:
    HierarchyPanel() = default;

    void OnImGuiRender(entt::registry& reg);

   private:
    void DrawEntityNode(entt::entity entity, entt::registry& reg);
    void HandleShortcuts(entt::registry& reg);

    // Вспомогательные методы для работы с иерархией лучше тоже держать здесь,
    // чтобы не засорять глобальное пространство имен
    void ReparentEntity(entt::entity child, entt::entity newParent, entt::registry& reg);
    bool IsDescendantOf(entt::entity child, entt::entity parent, entt::registry& reg);
    entt::entity CloneEntity(entt::entity srcEntity, entt::registry& reg);

    void DestroyEntityRecursive(entt::entity entity, entt::registry& reg);

   private:
    // Состояние панели (UI State)
    std::vector<entt::entity> m_EntitiesToDestroy;
    std::vector<entt::entity> m_ClipboardEntities;

    entt::entity m_EntityToRename = entt::null;
    char m_RenameBuffer[256] = "";

    bool m_IsCutOperation = false;
};
}  // namespace editor
